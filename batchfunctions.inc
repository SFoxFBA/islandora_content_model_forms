<?php

function cloneobject($pid, $selectedcollection, &$context) {
  $policydsid = 'POLICY';
  $fedora_object = islandora_object_load($pid);
  $clonedobject = islandora_copy_object($fedora_object);
  //Need to ignore the POLICY datastream during cloning.
  $clonedobject->purgeDatastream($policydsid);
  //need to change the objectId (pid) of the cloned object
  $connection = islandora_get_tuque_connection();
  $repository = $connection->repository;
  $newpid = $repository->getNextIdentifier($selectedcollection);
  $clonedobject->id = $newpid;

  $ingestobject = islandora_add_object($clonedobject);
  if ($ingestobject) {
    //if the ingest works then
    $ingestsuccessful = TRUE;
  }
  else {
    //if the ingest fails then
    $ingestsuccessful = FALSE;
  }
  //Remove the restrictive POLICY datastream, and replace with an open one,
  //dependant on the Metadata only flag.
  $dtca = $ingestobject['DTCA'];
  $dtcacontent = $dtca->content;
  $dtcads = simplexml_load_string($dtcacontent);
  $metadataonlytext = (string) $dtcads->publishMetadataOnly;

  if (strcmp($metadataonlytext, 'Yes') === 0) {
    sidora_update_datastream($clonedobject, create_published_xacml(), $policydsid, NULL, 'text/xml');
  }
  else {
    sidora_update_datastream($clonedobject, create_open_xacml(), $policydsid, NULL, 'text/xml');
  }

  $context['results']['success'][] = array($pid, $newpid, $ingestsuccessful);

  return;
}

function create_published_xacml(stdClass $account = NULL) {
  module_load_include('inc', 'islandora_xacml_api', 'Xacml');
  global $user;
  $account = is_object($account) ? $account : $user;
  $xacml = new Xacml();
  $xacml->managementRule->addUser(array('fedoraAdmin', $account->name));
  $xacml->managementRule->addRole(array('administrator'));
  //Removed next two lines, so all users can view unrestricted datastreams
  //$xacml->viewingRule->addUser(array('fedoraAdmin', $account->name));
  //$xacml->viewingRule->addRole(array('administrator'));
  $xacml->datastreamRule->addDsid('RELS-INT');
  $xacml->datastreamRule->addDsid('TN');
  $xacml->datastreamRule->addDsid('EXIF');
  $xacml->datastreamRule->addDsid('MEDIUM_SIZE');
  $xacml->datastreamRule->addDsid('TECHMD_FITS');
  $xacml->datastreamRule->addDsidRegex('^OBJ');
  $xacml->datastreamRule->addDsidRegex('^COLS');
  $xacml->datastreamRule->addUser(array('fedoraAdmin', $account->name));
  $xacml->datastreamRule->addRole(array('administrator'));

  $xml = $xacml->getXmlString(TRUE);
  return $xml;
}

function create_open_xacml(stdClass $account = NULL) {
  module_load_include('inc', 'islandora_xacml_api', 'Xacml');
  global $user;
  $account = is_object($account) ? $account : $user;
  $xacml = new Xacml();
  $xacml->managementRule->addUser(array('fedoraAdmin', $account->name));
  $xacml->managementRule->addRole(array('administrator'));
  //$xacml->viewingRule->addUser(array('fedoraAdmin', $account->name));
  //$xacml->viewingRule->addRole(array('administrator'));
  $xml = $xacml->getXmlString(TRUE);
  return $xml;
}

function tidyuprelations($batchresults, $selectedcollectionns, &$context) {
  global $user;

  foreach ($batchresults as $batchresult) {
    $privatepid = $batchresult[0];
    $publicpid = $batchresult[1];
    $object = islandora_object_load($publicpid);

    $lastchainpid = findlastpidinchain($privatepid, 'http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasPublishedVersion');

    foreach ($object as $dsid => $datastream) {
      switch ($dsid) {
        case 'DC': {
            //Apply the new publish transformation 
            $transform = drupal_get_path('module', 'islandora_content_model_forms') . "/transforms/dtca_to_dc_publish.xsl";
            $xsl = new DOMDocument();
            $xsl->load($transform);
            $xslt = new XSLTProcessor();
            $xslt->importStyleSheet($xsl);
            $dtca = $object['DTCA'];
            $dtcacontent = $dtca->content;
            $ds = DOMDocument::loadXML($dtcacontent);
            $transformeddoc = $xslt->transformToDoc($ds);
            $topleveldc = $transformeddoc->getElementsByTagNameNS('http://www.openarchives.org/OAI/2.0/oai_dc/', 'dc')->item(0);
            $dcid = $transformeddoc->createElement('dc:identifier', $publicpid);
            $topleveldc->appendChild($dcid);
            $object['DC']->content = $transformeddoc->saveXML();

            break;
          }
        case 'RELS-EXT': {
            //isMemberOfCollection fbauser<xxx>:collection needs swapping to dtc<xxxx>
            $rels = $object->relationships;
            $imocs = $rels->get(FEDORA_RELS_EXT_URI, 'isMemberOfCollection');
            foreach ($imocs as $imoc) {
              $imocvalue = $imoc[object][value];
              $user_id = $user->islandora_user_pid;
              $user_collection_ns = str_replace(array('-', ':'), '', $user_id);
              $user_collection = $user_collection_ns . ':collection';
              if (strcmp($imocvalue, $user_collection) === 0) {
                $rels->remove(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $imocvalue);
                $rels->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $selectedcollectionns . ':collection');
              }
            }
            //isComponentInputOf dtcan:xx needs swapping to the published (eg) analysis pid
            swaprelsvalues('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'isComponentInputOf', $batchresults, $object);

            //Handle hasXXXX relsext here
            $allrdfs = array_merge(sidora_return_forward_fba_concept_rdfs(), sidora_return_forward_fba_resource_rdfs());
            foreach ($allrdfs as $allrdf) {
              swaprelsvalues(RELS_EXT_URI, $allrdf, $batchresults, $object);
            }
            break;
          }
        case 'DTCA': {
            //the new objects archival version number needs setting to 1 greater 
            //than current highest
            $dtca = $object['DTCA'];
            $dtcacontent = $dtca->content;
            $sxmldtca = new SimpleXMLElement($dtcacontent);
            $sxmldtca->archivalPublicationDate = date("Y-m-d H:i:s");
            //Version number needs to be taken from last published in the chain
            if (is_null($lastchainpid)) {
              $sxmldtca->archivalPublicationVersion++;
            }
            else {
              $lastchainobject = islandora_object_load($lastchainpid[0]);
              $lastchaindtca = $lastchainobject['DTCA'];
              $lastchaindtcacontent = $lastchaindtca->content;
              $lastchainsxmldtca = new SimpleXMLElement($lastchaindtcacontent);
              $lastchainversion = (int) $lastchainsxmldtca->archivalPublicationVersion;
              $sxmldtca->archivalPublicationVersion = $lastchainversion + 1;
            }

            $object['DTCA']->content = $sxmldtca->asXML();
            break;
          }
        case 'RELS-INT': {
            //change the rdf:description/rdf:about value.
            $ri = $object['RELS-INT'];
            $fri = new IslandoraFedoraRelsInt($ri);
            $fri->changeObjectID($publicpid);

            //change the hasConstituent pid value, remembering that it has a suffix of (eg) /COLS-1-1
            $ricontent = $ri->content;
            $newricontent = str_replace($privatepid, $publicpid, $ricontent);
            $object['RELS-INT']->content = $newricontent;
            break;
          }
        default: {
            if (strpos($dsid, 'COLS') !== FALSE) { //Could also be DTCA, POLICY, OBJ<n>
              //COLSx-x datastream
              //<rdf:description rdf:about> needs pid swapping
              //rdf:description/dc:identifier needs pid swapping out, noting that there's a suffix 
              $ri = $object[$dsid];
              $ricontent = $ri->content;
              $newricontent = str_replace($privatepid, $publicpid, $ricontent);
              $object[$dsid]->content = $newricontent;
            }
          }
      }
    }
    //Need to add the hasPublishedVersion relsext now, to the last object in the chain
    if (is_null($lastchainpid)) {
      $pidtoupdate = $privatepid;
      $pidtopointto = $publicpid;
    }
    else {
      $pidtoupdate = $lastchainpid[0];
      $pidtopointto = $publicpid;
    }

    $privateobject = islandora_object_load($pidtoupdate);
    $privrels = $privateobject->relationships;
    $privrels->add('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasPublishedVersion', $pidtopointto);
  }
}

function swaprelsvalues($namespace, $relation, $batchresults, $object) {
  $rels = $object->relationships;
  $relations = $rels->get($namespace, $relation);
  foreach ($relations as $relvalue) {
    $relobject = $relvalue[object];
    $objvalue = $relobject[value];
    foreach ($batchresults as $translationpair) {
      if (strcmp($objvalue, $translationpair[0]) === 0) {
        //Can swap this over as there's a match
        $rels->remove($namespace, $relation, $objvalue);
        $rels->add($namespace, $relation, $translationpair[1]);
      }
    }
  }
}

function findlastpidinchain($pid, $namespace, $relation) {
  $object = islandora_object_load($pid);
  $rels = $object->relationships;
  $foundrelations = $rels->get($namespace, $relation);
  if ($foundrelations) {
    $foundrelpid = $foundrelations[0][object][value];
    $foundrelreturnpid = findlastpidinchain($foundrelpid, $namespace, $relation);
    return array($foundrelpid, $foundrelreturnpid);
  }
  else {
    return NULL;
  }
}

function _finished($success, $results, $operations) {
  if ($success) {
    //drupal_set_message(format_plural(count($results['success']), 'One done.', '@count done.'));
    $_SESSION['my_batch_succeeded'] = TRUE;
    $_SESSION['my_batch_results'] = $results;
  }
  else {
    //$message = t('Finished with an error.');
    $_SESSION['my_batch_succeeded'] = FALSE;
  }
}
