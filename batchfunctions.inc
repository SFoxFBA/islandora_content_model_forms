<?php

function cloneobject($pid, $selectedcollection, &$context) {

  $fedora_object = islandora_object_load($pid);
  $clonedobject = islandora_copy_object($fedora_object);
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

  $context['results']['success'][] = array($pid, $newpid, $ingestsuccessful);

  return;
}

function tidyuprelations($batchresults, $selectedcollectionns, &$context) {
  foreach ($batchresults as $batchresult) {
    $privatepid = $batchresult[0];
    $publicpid = $batchresult[1];
    $object = islandora_object_load($publicpid);

    foreach ($object as $dsid => $datastream) {
      switch ($dsid) {
        case 'DC': {
            //needs the original dc:identifier removing
            $dccontent = $object['DC']->content;
            $objectDoc = new domDocument();
            $objectDoc->loadXML($dccontent);
            $objectXpath = new domXpath($objectDoc);
            $removeNodeQueryResults = $objectXpath->query('//dc:identifier[text()="' . $privatepid . '"]');
            $removeNodeQueryResultsTot = $removeNodeQueryResults->length;
            if ($removeNodeQueryResultsTot > 0) {
              foreach ($removeNodeQueryResults as $node) {
                $nodeval = $node->nodeValue;
                if (!strcmp($nodeval, $privatepid)) {
                  $childNode = $node;
                  $parentNode = $childNode->parentNode;
                  $node = $parentNode;
                  $parentNode->removeChild($childNode);
                }
              }
            }
            $object['DC']->content = $objectDoc->saveXML();
            break;
          }
        case 'RELS-EXT': {
            //isMemberOfCollection fbauser<xxx>:collection needs swapping to dtc<xxxx>

            $rels = $object->relationships;
            $imocs = $rels->get(FEDORA_RELS_EXT_URI, 'isMemberOfCollection');
            foreach ($imocs as $imoc) {
              $imocobject = $imoc[object];
              $imocvalue = $imocobject[value];
              global $user;
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
    $lastchainpid = findlastpidinchain($privatepid, 'http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasPublishedVersion');
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
    $foundrelationpid = $foundrelations[0][object][value];
    $foundrelationsreturnpid = findlastpidinchain($foundrelationpid, $namespace, $relation);
    return array($foundrelationpid, $foundrelationsreturnpid);
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

  /*
    if ($results['fail']) {
    foreach ($results['fail'] as $fail) {
    drupal_set_message("Failed to do: $fail", 'error');
    }
    } */
}
