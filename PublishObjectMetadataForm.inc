<?php

/*
 * @file
 *
 */
module_load_include('inc', 'fedora_repository', 'api/fedora_item');
module_load_include('inc', 'fedora_repository', 'ContentModel');
module_load_include('inc', 'xml_form_api', 'XMLForm');

/**
 * FIXME:  This is just a collection of functions; there is no object state.
 *   All functions should be made static and so accessed.
 */
class PublishObjectMetadataForm {

  /**
   * Get Content Models for the form.
   *
   * @param string $collection_pid
   *
   * @return array
   */
  public function getPotentialContentModels($collection_pid) {
    if (($collection_policy = CollectionPolicy::loadFromCollection($collection_pid)) === FALSE) {
      drupal_set_message(t('Unable to load collection policy \'' . $collection_pid . '\'.'));
      return FALSE;
    }
    if (!($content_models = $collection_policy->getContentModels())) {
      drupal_set_message(t('No content models associated with this collection: !collection_label. Please contact your administrator.', array('!collection_label' => $collection_label)), 'error');
      return FALSE;
    }
    $potential_models = array();
    foreach ($content_models as $content_model) {
      $identifier = $content_model->getIdentifier();
      $name = $content_model->name;
      $potential_models["$identifier"] = "$name";
    }
    return $potential_models;
  }

  public function getFieldsRequiringValidation($pid, $sxmldtca) {
    module_load_include('inc', 'xml_form_builder', 'XMLFormDatabase');
    module_load_include('inc', 'xml_form_api', 'XMLForm');
    module_load_include('inc', 'xml_form_api', 'XMLFormDefinition');
    module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');


    module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
    module_load_include('inc', 'fedora_repository', 'ObjectHelper');

    $objectHelper = new ObjectHelper();
    $content_models = $objectHelper->get_content_models_list($pid);
    foreach ($content_models as $content_model) {
      if (strpos($content_model->pid, 'si:') === FALSE) { //SFOX horrible hack to ignore si content models
        $content_model_pid = $content_model->pid;
      }
    }
    $object_linker_form_name = get_object_linker_form_name($pid, $content_model_pid);
    $olformdatabase = XMLFormDatabase::Get($object_linker_form_name);
    $object_linker = new XMLFormDefinition($olformdatabase);
    $object_linker_definition = $object_linker->definition;
//$test = $object_linker_definition->saveXML();
    $object_linker_form = $object_linker->getForm();
//compile the list of form fields that have "fieldvalidator" as an attribute

    $xpath = new DOMXPath($object_linker_definition);
    $results = $xpath->query("//fieldvalidator");
    $rescount = $results->length;

    $validationobjects = array();
    for ($i = 0; $i < $rescount; $i++) {
//need to get 'name' from topmost element in nodepath
      $node = $results->item($i);
      $nodepath = $node->getNodePath();
      $name = $node->nodeName;
      $val = $node->nodeValue;
//try and find actions/read/path
      $readpos = strpos($nodepath, 'attributes/fieldvalidator');
      $readprefix = substr($nodepath, 0, $readpos);
      $readpath = $readprefix . '/actions/read/path';
      $readval = $xpath->evaluate($readpath);
      $readvalitem = $readval->item(0)->nodeValue;
      //remove the '/dtca:xxxx/' prefix
      $sxmldtca->registerXPathNamespace('dtca', 'http://fba.org.uk/schemas/dtca/1.0.0');
      $sxmldtca->registerXPathNamespace('gco', 'http://www.isotc211.org/2005/gco');
      $xpathresult = $sxmldtca->xpath($readvalitem);
      $xpathresulttext = (string) $xpathresult[0];

      $pos = strpos($nodepath, 'properties/attributes/fieldvalidator');
      $prefix = substr($nodepath, 0, $pos);
      $titlepath = $prefix . '/title';
      $titleval = $xpath->evaluate($titlepath);
      $titlevalitem = $titleval->item(0)->nodeValue;
      $continueloop = TRUE;
      while (is_null($titlevalitem) && $continueloop) {
        $pos = strrpos($prefix, 'children/element');
        $prefix = substr($nodepath, 0, $pos);
        if (strpos($prefix, 'element') === FALSE) {
          $continueloop = FALSE;
        }
        else {
          $titlepath = $prefix . '/title';
          $titleval = $xpath->evaluate($titlepath);
          $titlevalitem = $titleval->item(0)->nodeValue;
        }
      }
      if (is_null($titlevalitem)) {
        $titlevalitem = 'Warning - title field missing';
      }
      $validationobjects[] = array($nodepath, $titlevalitem, $val, $readvalitem, $xpathresulttext);
    }
//set up array of grouped fields
    foreach ($validationobjects as $valobjectkey => $validationobject) {
      if (ctype_upper(substr($validationobject[2], 1, 1))) {
//Upper case second letter, this field is part of a group
        $fieldgroups[substr($validationobject[2], 1, 1)][] = $valobjectkey;
      }
      else
      if (strpos($validationobject[2], 'voc') !== FALSE) {
        $checkagainstvocab[] = $valobjectkey;
      }
      else
      if (strpos($validationobject[2], 'geo') !== FALSE) {
        $checkagainstgeovocab[] = $valobjectkey;
      }
      else
        $simplecheck[] = $valobjectkey;
    }
// validate each of the fieldgroups 

    return $validationobjects;
  }

  /**
   * Create page one.
   *
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   *
   * @return array
   */
  public function createPageOne($pid, $client, array &$form_state) {
    $dtcadatastream = $this->getMetadata($pid, 'DTCA', $client);
    $sxmldtca = simplexml_load_string($dtcadatastream);
    $datasetfieldsrequiringvalidation = $this->getFieldsRequiringValidation($pid, $sxmldtca);

    $currentrels = array();
    $futurerels = array();
    $repository = tuque_wrapper_get_repository_instance();
    $objecttopublish = $repository->getObject($pid);
    $relationships = $objecttopublish->relationships->get('info:fedora/fedora-system:def/relations-external#', 'isMemberOfCollection');

    //Is this in an 'fbauserNN:collection'?
    $ismemberofusercollection = FALSE;
    foreach ($relationships as $relationship) {
      $currentrels[$relationship[object][value]] = $relationship[object][value];
      $fbauserpos = strpos($relationship[object][value], 'fbauser');
      if ($fbauserpos !== FALSE) {
        if ($fbauserpos == 0) {
          $ismemberofusercollection = TRUE;
          $fbausercollparent = $relationship[object][value];
        }
      }
    }

    $ri = $repository->ri;

	//Which parent activities have a Concept type relation to this object?
	//This needs to be an (upwards) recursive loop stopping at fbauser<nn> level
    $conceptrdfs = sidora_return_forward_fba_concept_rdfs();
    $arraycount = count($conceptrdfs);

    $query = 'select ?subject where {';
    foreach ($conceptrdfs as $key => $keyvalue) {
      $query .= '{?subject <info:fedora/fedora-system:def/relations-external#' . $keyvalue . '> <info:fedora/' . $pid . '>}';
      if ($key < $arraycount - 1) { //as arraycounts don't start at zero
        $query .= ' UNION ';
      }
    }
    $query .= '}';

    $sparql_query_results = $ri->sparqlQuery($query);
    foreach ($sparql_query_results as $sparql_query_result) {
      $parentconcepts[] = $sparql_query_result[subject][value];
    }

    //Does this object have any resources?
    $resourcerdfs = sidora_return_forward_fba_resource_rdfs();
    $arraycount = count($resourcerdfs);

    $query = 'select ?object where {';
    foreach ($resourcerdfs as $key => $keyvalue) {
      $query .= '{<info:fedora/' . $pid . '> <info:fedora/fedora-system:def/relations-external#' . $keyvalue . '> ?object}';
      if ($key < $arraycount - 1) { //as arraycounts don't start at zero
        $query .= ' UNION ';
      }
    }
    $query .= '}';


    $sparql_query_results = $ri->sparqlQuery($query);
    foreach ($sparql_query_results as $sparql_query_result) {
      $childresources[] = $sparql_query_result[object][value];
    }

    //Which collections can this object be published to?
    $query = 'select ?subject where {?subject <info:fedora/fedora-system:def/relations-external#isMemberOfCollection> <info:fedora/dtc:collection>}';

    $sparql_query_results = $ri->sparqlQuery($query);
    foreach ($sparql_query_results as $sparql_query_result) {
      if (strpos($sparql_query_result[subject][value], ':root') === FALSE) { //Bodged code to remove roots for now until MH decides final format
        $futurerels[$sparql_query_result[subject][value]] = $sparql_query_result[subject][value];
      }
    }
    $remainingrels = array_diff($futurerels, $currentrels); //Don't want to try and re-add it to an existing collection
    //foreach ($remainingrels as $rel) {
//      $relsarray .= '<option value=' . $rel . '>' . $rel . '</option>';
//    }

    $currentrelstext = '<br/><h1>' . $pid . '</h1>';
    $currentrelstext .= '<br/>Currently in collection(s) <br/>';
    if ($currentrels) {
      foreach ($currentrels as $currentrel) {
        $currentrelstext .= ' - ' . $currentrel;
      }
    }
    else {
      $currentrelstext .= ' - none';
    }
    $currentrelstext .= '<br/>';

    //print out parent concepts
    if ($parentconcepts) {
      $currentrelstext .= '<br/><h2>Parent Concepts</h2>';
      foreach ($parentconcepts as $parentconcept) {
        $currentrelstext .= '</br> - ' . $parentconcept;
      }
    }
    else {
      $currentrelstext .= '<br/><h2>Parent Concepts</h2><br/> - none';
    }
    $currentrelstext .= '<br/>';

    //print out child resources
    $csvallpresent = TRUE;
    if ($childresources) {
      $currentrelstext .= '<br/><h2>Child Resources</h2>';
      foreach ($childresources as $childresource) {
        $currentrelstext .= '</br> - ' . $childresource;
        //Also need to check if the child resources has a data component,
        // and that that data component has a csv file attached
        $childresourceobject = $repository->getObject($childresource);

        $objdatastream = $childresourceobject->getDatastream('OBJ1');

        if ($objdatastream === FALSE) {
          $currentrelstext .= '&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;OBJ1 DATASTREAM MISSING';
          $csvallpresent = FALSE;
        }
        else {
          $currentrelstext .= '&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;OBJ1 datastream found';
        }
        $currentrelstext .= '<br/>';

// Need to confirm child resources have the correct number of inputs
// put a function in here to return the number of inputs maybe xpath on <DTCA datastream, dtca:input/<gco:CharacterString, <keyword authority
        $dtcadatastream = $this->getMetadata($childresource, 'DTCA', $client);

        $sxmldtca = simplexml_load_string($dtcadatastream);

        if ($sxmldtca) {
          $sxmldtca->registerXPathNamespace('dtca', 'http://fba.org.uk/schemas/dtca/1.0.0');
          $sxmldtca->registerXPathNamespace('gco', 'http://www.isotc211.org/2005/gco');
          $xpathresult = $sxmldtca->xpath('//dtca:input/gco:CharacterString');
          $numinputs = 0;
          foreach ($xpathresult as $foundinput) {
            if (strlen((string) $foundinput) > 0) {
              $numinputs++;
            }
          }

          $numinputmessage = 'This ';
          $pidns = substr($childresource, 0, strpos($childresource, ':'));
          switch ($pidns) {
            case 'dtcan': {
                $numinputmessage .= 'analysis has ' . $numinputs . ' input(s)';
                if ($numinputs > 1) {
                  $validnumberofinputs[] = TRUE;
                  $numinputmessage .= ' - Passed<br/>';
                }
                else {
                  $validnumberofinputs[] = FALSE;
                  $numinputmessage .= ' - Failed<br/>';
                }
                break;
              }
            case 'dtcmt': {
                $numinputmessage .= 'measurement has ' . $numinputs . ' input(s)<br/>';
                if ($numinputs = 0) {
                  $validnumberofinputs[] = TRUE;
                  $numinputmessage .= ' - Passed<br/>';
                }
                else {
                  $validnumberofinputs[] = FALSE;
                  $numinputmessage .= ' - Failed<br/>';
                }
                break;
              }
            case 'dtclr': {
                $numinputmessage .= 'literature review has ' . $numinputs . ' input(s)<br/>';
                if ($numinputs > 0) {
                  $validnumberofinputs[] = TRUE;
                  $numinputmessage .= ' - Passed<br/>';
                }
                else {
                  $validnumberofinputs[] = FALSE;
                  $numinputmessage .= ' - Failed<br/>';
                }
                break;
              }
            case 'dtcsy': {
                $numinputmessage .= 'synthesis has ' . $numinputs . ' input(s)<br/>';
                if ($numinputs > 2) {
                  $validnumberofinputs[] = TRUE;
                  $numinputmessage .= ' - Passed<br/>';
                }
                else {
                  $validnumberofinputs[] = FALSE;
                  $numinputmessage .= ' - Failed<br/>';
                }
                break;
              }
            case 'dtcsi': {
                $numinputmessage .= 'simulation has ' . $numinputs . ' input(s)<br/>';
                if ($numinputs > 0) {
                  $validnumberofinputs[] = TRUE;
                  $numinputmessage .= ' - Passed<br/>';
                }
                else {
                  $validnumberofinputs[] = FALSE;
                  $numinputmessage .= ' - Failed<br/>';
                }
                break;
              }
            default:
              break;
          }
          $currentrelstext .= $numinputmessage;
        }
        else {
//DTCA datastream is missing, so no validation required. For supplemantary files
          $validnumberofinputs[] = TRUE;
        }
      }
    }
    else {
      $currentrelstext .= '<br/><h2>Child Resources</h2><br/> - none';
      $csvallpresent = FALSE;
    }
//all have to have valid number of inputs to pass
    $passedonallinputs = TRUE;
    if ($validnumberofinputs) { //are there any values to work on?
      foreach ($validnumberofinputs as $passflag) {
        if (!$passflag) {
          $passedonallinputs = FALSE; //don't have to look for another Fail after first has been found
          break;
        }
      }
    }
    $currentrelstext .= '<br/>';

    //check if the object's metadata has correctly completed title and abstract fields
    $dtcadatastream = $this->getMetadata($pid, 'DTCA', $client);

    $sxmldtca = simplexml_load_string($dtcadatastream);

    $dtcatitle = (string) $sxmldtca->title;
    $dtcaabstract = (string) $sxmldtca->abstract;

    $currentrelstext .= '<br/><h1>Metadata values</h1><br/>';
    $currentrelstext .= 'DTCA title - ';
    if ($dtcatitle) {
      $currentrelstext .= $dtcatitle;
    }
    else {
      $currentrelstext .= 'MISSING';
    }
    $currentrelstext .= '<br/>';
    $currentrelstext .= 'DTCA abstract - ';
    if ($dtcaabstract) {
      $currentrelstext .= $dtcaabstract;
    }
    else {
      $currentrelstext .= 'MISSING';
    }
    $currentrelstext .= '<br/>';
    $currentrelstext .= '<br/><h1>Subject and Geovocab terms</h1><br/>';

    $validgeosubjectkeywords = FALSE;
    $dtcakeywords = $sxmldtca->keyword;
    $numkeywords = $dtcakeywords->count();

    foreach ($dtcakeywords as $dtcakeyword) {
      $keywordattrs = $dtcakeyword->attributes();
      foreach ($keywordattrs as $keywattr => $keywvalue) {
        $keywvaluestring = (string) $keywvalue;
      }
      $dtcakeywordchild = $dtcakeyword->children('http://www.isotc211.org/2005/gco');
      foreach ($dtcakeywordchild as $keycharstring => $sxekeyword) {
        $sxekeywordstr = (string) $sxekeyword;
//why is this not returning a string!?!?
      }
    }
    if ($ismemberofusercollection && $parentconcepts && $csvallpresent && $dtcatitle && $dtcaabstract && $passedonallinputs && $validgeosubjectkeywords) {
      $form = array(
        'currentcollections' => array(
          '#type' => 'markup',
          '#value' => $currentrelstext,
        ),
        'publishcollections' => array(
          '#type' => 'select',
          '#title' => t('Publish to collection'),
          '#options' => $remainingrels,
        ),
        'pidtopublish' => array(
          '#type' => 'hidden',
          '#value' => $pid,
        ),
        'submit' => array(
          '#type' => 'submit',
          '#submit' => array('fedora_repository_publish_qdc_form_submit'),
          '#value' => 'Next'
        )
      );
    }
    else {
      $form = array(
        'currentcollections' => array(
          '#type' => 'markup',
          '#value' => $currentrelstext,
        ),
      );
    }
    return $form;
  }

  /**
   * Create page two.
   *
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   *
   * @return array
   */
  public function createPageTwo($pid, $client, array &$form_state) {
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');

    $licenceprivacytext = 'Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam ipsum velit, viverra quis nunc at, efficitur efficitur risus. Plus text to explain licensing. <br/><br/>';
    $selectedcollection = $form_state['values']['publishcollections'];
    $selectedpid = $form_state['values']['pidtopublish'];
    $licenceprivacytext .= $selectedpid . ' has been chosen for publishing to ' . $selectedcollection . '.<br/><br/>';

    $form = array(
      'currentcollections' => array(
        '#type' => 'markup',
        '#value' => $licenceprivacytext,
      ),
      'pidtopublish' => array(
        '#type' => 'hidden',
        '#value' => $pid,
      ),
      'selectedcollection' => array(
        '#type' => 'hidden',
        '#value' => $selectedcollection,
      ),
      'submit' => array(
        '#type' => 'submit',
        '#submit' => array('fedora_repository_publish_qdc_form_submit'),
        '#value' => 'Next'
      )
    );
    $form['forms'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['forms'],
    );
    $form['pid'] = array(
      '#type' => 'hidden',
      '#value' => $pid,
    );
    return $form;

    /* FBA version    
      module_load_include('inc', 'fedora_repository', 'api/fedora_item');

      $licenceprivacytext = 'Some stuff will go here to explain licensing. <br/><br/>';
      $selectedcollection = $form_state['values']['publishcollections'];
      $selectedpid = $form_state['values']['pidtopublish'];
      $licenceprivacytext .= $selectedpid . ' has been chosen for publishing to ' . $selectedcollection . '.<br/><br/>';
      $item = new Fedora_Item($pid);
      $content_model = ContentModel::loadFromObject($pid);
      $content_model_pid = $content_model->pid;
      $form_name = $form_state['values']['forms'];

      $dsid = islandora_content_model_get_dsid($content_model->pid, $form_name);
      $xml = NULL;
      if (array_key_exists($dsid, $item->datastreams)) {
      $xml = $this->getMetadata($pid, $dsid, $client);
      }
      $form = xml_form_builder_get_form($form_state, 'Publish Dataset Legals', $xml);
      $form['forms'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['forms'],
      );
      $form['content_model_pid'] = array(
      '#type' => 'hidden',
      '#value' => $content_model_pid,
      );
      $form['pid'] = array(
      '#type' => 'hidden',
      '#value' => $pid,
      );
      $form['dsid'] = array(
      '#type' => 'hidden',
      '#value' => isset($form_state['values']['dsid']) ? $form_state['values']['dsid'] : $dsid,
      );

      $form = array('licenseprivacy' => array(
      '#type' => 'markup',
      '#value' => $licenceprivacytext,
      ),
      'selectedcollection' => array(
      '#type' => 'hidden',
      '#value' => $selectedcollection,
      ),
      'selectedpid' => array(
      '#type' => 'hidden',
      '#value' => $selectedpid,
      ),
      );

      return $form;
     * End of FBA version
     */
  }

  public function createPageThree($pid, $client, array &$form_state) {
    module_load_include('inc', 'fedora_repository', 'ObjectHelper');
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    module_load_include('inc', 'content_model_viewer', 'Collection');

    global $base_path;
    global $base_root;
    global $user;

    $user_data = unserialize($user->data);
    $user_pid = $user_data['islandora_user_pid'];
    $usernumber = substr($user_pid, strpos($user_pid, ':') + 1); //+1 to ignore the colon
//Add the relsext here.
    $selectedpid = $form_state['values']['pidtopublish'];
    $selectedcollection = $form_state['values']['selectedcollection'];

    $repository = tuque_wrapper_get_repository_instance();
    $objecttopublish = $repository->getObject($selectedpid);
//Find which descendants will need publishing
    $collection = new Collection($selectedpid);
    list($descendants, $totaldescs) = $collection->getMembers();
    foreach ($descendants as $descendant) {
      $descendantlist[] = $descendant['pid'];
    }
//now find ancestors
    $queryprefix = 'select $subject from <#ri> where { ?object <fedora-model:state> <fedora-model:Active>; {$subject <fedora-rels-ext:hasDataset> <info:fedora/';
    $querysuffix = '>}}';
    $query = $queryprefix . $selectedpid . $querysuffix;
    $sparqlresult = ObjectHelper::performRiQuery($query, 'sparql');

    if ($sparqlresult) {
      foreach ($sparqlresult as $key => $valuearray) {
        if (strcmp($user_pid, $valuearray['subject']) != 0) { //the object might be nextmost level to user pid
          $ancestorlist[] = $valuearray['subject']; //overall publishing list
          $parentlist[] = $valuearray['subject']; //immediate parents
        }
      }
    }//a check needs putting in here for no returned pids!

    if ($parentlist) {
      $queryprefix = 'select $subject from <#ri> where { ?object <fedora-model:state> <fedora-model:Active>; {$subject <fedora-rels-ext:hasActivity> <info:fedora/';
      foreach ($parentlist as $key => $parentpid) {
        $founduser = 1;
//walk up each ancestral path, stopping at user level
        $query = $queryprefix . $parentpid . $querysuffix;
        while ($founduser == 1) {
          $sparqlresult = ObjectHelper::performRiQuery($query, 'sparql');
          $sparqlresultpid = $sparqlresult[0]['subject'];
          $founduser = strcmp($user_pid, $sparqlresultpid);
          if ($founduser == 1) {
            $ancestorlist[] = $sparqlresultpid;
          }
          $query = $queryprefix . $sparqlresultpid . $querysuffix;
        }
      }
    }
//Insert the dataset pid at the top of the list of objects to be processed
    if ($ancestorlist) {
      array_unshift($ancestorlist, $selectedpid);
    }
    else {
      $ancestorlist[] = $selectedpid;
    }

    $clonelist = array_merge($descendantlist, $ancestorlist);

//enumerate through clone list to create new objects with prefix "dtcpub<usernumber>:<nnn>"
    $numberrequired = count($clonelist);

//for ($i = 0; $i < $numberrequired; $i++) {
//$frompid = $clonelist[$i];
//$topid = $newpids[$i];
//$currentobject = $repository->getObject($frompid);
//$newobject = $repository->constructObject('dtcpub' . $usernumber);
//$newobject->label = $currentobject->label;
//foreach ($currentobject as $dsid => $content) {
//will need to go through relsext/relsint of new objects and put translated pids in to keep the inheritance correct.
//$newobject->ingestDatastream($content);
//$newds = $newobject[$dsid];
//$newds->relationships->changeObjectID($newobject->id);
//switch ($dsid) {
//  case 'RELS-EXT':
//    $rels = $currentobject->relationships->get();
//    $newds->relationships->remove(FEDORA_MODEL_URI, 'isMemberOfCollection', 'info:fedora/fbauser' . $usernumber . ':collection');
//    break;
//  case 'DC':
//$newds->relationships->remove(FEDORA_MODEL_URI, 'isMemberOfCollection', 'fbauser' . $usernumber . ':collection');
//    break;
//  default:
//    break;
// }
//}
//$rel = $newobject->relationships;
//$rel->changeObjectID($newobject->id); //changes rels-ext only
//$ingested_object = $repository->ingestObject($newobject);
//$ingested_object->relationships->add(RELS_EXT_URI, 'isMemberOfCollection', $selectedcollection);
//old dc:identifier pid is still in there
//COLS datastreams still have info:fedora/dtcan:13/OBJ1 and <dc:identifier>dtcan:13-1-0</dc:identifier>
//RELS-INT still has <relsext:hasConstituent rdf:resource="info:fedora/dtcan:13/COLS1-1"/><relsext:hasConstituent rdf:resource="info:fedora/dtcan:13/COLS1-2"/><relsext:hasConstituent rdf:resource="info:fedora/dtcan:13/COLS1-3"/><relsext:hasConstituent rdf:resource="info:fedora/dtcan:13/COLS1-4"/>
//}


    $redirecturl = $base_root . $base_path . 'fedora/repository/' . $pid;

    $debugstring = '';
    foreach ($clonelist as $clonepid) {
      $debugstring.= $clonepid . ' ,<br/> ';
    }
    $form = array(
      'currentcollections' => array(
        '#type' => 'markup',
        '#value' => 'These are the objects to be cloned/published: <br/>' . $debugstring,
      ),
      'pidtopublish' => array(
        '#type' => 'hidden',
        '#value' => $pid,
      ),
      'finish' => array(
        '#type' => 'markup',
        '#value' => '<a href=' . $redirecturl . ' target="_parent">Needs a link here to return to the object screen (the same as if cancel had been clicked)</a>',
      ),
      'forms' => array(
        '#type' => 'hidden',
        '#value' => $form_state['values']['forms'],
      ),
    );
//use this instead?     $form_state['redirect'] = $base_url . '/fedora/repository/' . $pid;

    return $form;
  }

  private function getMetadata($pid, $dsid, $client) {
    $params = array('pid' => $pid, 'dsID' => $dsid, 'asOfDateTime' => "");
    try {
      $object = $client->__soapCAll('getDatastreamDissemination', array('parameters' => $params));
    }
    catch (Exception $e) {
      return NULL;
    }
    return trim($object->dissemination->stream);
  }

  /**
   * Create forms.
   *
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   *
   * @return array
   */
  public function create($pid, $client, array &$form_state) {
    $page = $form_state['storage']['step'] = empty($form_state['storage']['step']) ? 1 : $form_state['storage']['step'];
    switch ($page) {
      case 1: {
          $form_state['storage']['xml'] = TRUE;
          return $this->createPageOne($pid, $client, $form_state);
          break;
        }
      case 2: {
          if ($form_state['storage']['xml']) {
            return $this->createPageTwo($pid, $client, $form_state);
          }
          else {
            throw new Exception('Couldn\'t Create the Publish Form.');
          }
          break;
        }
      case 3: {
          if ($form_state['storage']['xml']) {
            return $this->createPageThree($pid, $client, $form_state);
          }
          else {
            throw new Exception('Couldn\'t Create the Publish Form.');
          }
          break;
        }
      default: {
          throw new Exception('Couldn\'t Create the Publish Form.');
        }
    }
  }

  /**
   * Submit.
   *
   * @param array $form
   * @param array $form_state
   */
  public function submit(array &$form, array &$form_state) {
    global $base_url;
    $pid = $form_state['values']['pid'];
    $dsid = $form_state['values']['dsid'];
    $form_name = $form_state['values']['forms'];
    $content_model_pid = $form_state['values']['content_model_pid'];
    $label_field = islandora_content_model_get_title_field($content_model_pid, $form_name);
    //SFOX hacked code to get this working
    $exploded_label_field = explode('[', $label_field);
    foreach ($exploded_label_field as $elf_key => $elf_value) {
      if ($elf_value) {
        $label_field_text = trim($elf_value, ']');
        $label_field_text = str_replace("'", "", $label_field_text);
        if ($subarray) {
          if (array_key_exists($label_field_text, $subarray)) {
            $subarray = $subarray[$label_field_text];
          }
        }
        else {
          if (array_key_exists($label_field_text, $form_state['values'])) {
            $subarray = $form_state['values'][$label_field_text];
          }
          else {
            //not worth trying to drill down any further
            break;
          }
        }
      }
    }
    $label = $subarray;
    //SFOX
    //eval('$label = $form_state[\'values\']' . $label_field . ';');
    if (empty($label)) {
      $label = "Undefined";
    }
    $this->setObjectLabel($pid, $label);
    $document = $this->modifyMetadata($pid, $dsid, $form, $form_state);
    $transform = islandora_content_model_get_transform($content_model_pid, $form_name);
    $this->transform($document, $transform, $pid, $label);
    $form_state['storage'] = NULL;
    $form_state['redirect'] = $base_url . '/fedora/repository/' . $pid;
  }

  /**
   *
   * @param string $pid
   * @param string $dsid
   * @param array $form
   * @param array $form_state
   */
  private function modifyMetadata($pid, $dsid, array &$form, array &$form_state) {
    $xml_form = new XMLForm($form_state);
    $doc = $xml_form->submit($form, $form_state);
    $document = $doc->document;

    /**
     *
     * FBA MODIFICATION BY NBYWELL on 06/03/2014. Check the content of the
     * 'rdf_relationships' variable which triggers
     * the Object Linker functionality.
     *
     */
    if (variable_get('rdf_relationships', NULL)) {
      module_load_include('inc', 'islandora_content_model_forms', 'FOXML');
      module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');
      process_link_data($document);
    }
    $this->modifyDatastream($document, $pid, $dsid, "$dsid Record", 'text/xml');
    if (variable_get('rdf_relationships', NULL)) {
      process_links($form, $form_state, true);
    }
    return $document;
  }

  /**
   *
   * @param string $label
   */
  private function setObjectLabel($pid, $label) {
    // Because of FCREPO-1059 we need to make sure that any label we create doesn't
    // contain any newlines. This is valid for at least version 3.5 of Fedora.
    $label = str_replace(array("\r", "\r\n", "\n"), '', $label);

    $object = new Fedora_Item($pid);
    $object->modify_object($label); // Set the label
  }

  /**
   *
   * @param DOMDocument $document
   * @param string $transform
   * @param string $pid
   * @param string $label
   */
  private function transform(DOMDocument $document, $transform, $pid, $label) {
    $dublin_core = $this->transformDocumentIntoDublinCore($document, $transform);
    $xpath = new DOMXPath($dublin_core);
    // Set the Label
    $results = $xpath->query("*[local-name()='title']");
    $results->item(0)->nodeValue = htmlspecialchars(html_entity_decode($label, ENT_QUOTES, 'UTF-8'), ENT_QUOTES, 'UTF-8');
    // Set the Pid
    $results = $xpath->query("*[local-name()='identifier']");
    $results->item(0)->nodeValue = htmlspecialchars(html_entity_decode($pid, ENT_QUOTES, 'UTF-8'), ENT_QUOTES, 'UTF-8');
    $dublin_core->version = "1.0";
    $dublin_core->encoding = "UTF-8";
    $dublin_core->formatOutput = TRUE;
    $this->modifyDatastream($dublin_core, $pid, 'DC', "Dublin Core Record", 'text/xml');
  }

  /**
   * Transforms $document in to a dublin core document via xsl transform.
   *
   * @param DOMDocument $document
   * @param string $transform
   *
   * @return DOMDocument
   */
  private function transformDocumentIntoDublinCore(DOMDocument $document, $transform) {
    $xsl = new DOMDocument();
    $xsl->load($transform);
    $xslt = new XSLTProcessor();
    $xslt->importStyleSheet($xsl);
    $doc = new DOMDocument();
    $doc->loadXML($document->saveXML());
    return $xslt->transformToDoc($doc->documentElement);
  }

  /**
   * Test if a data stream is managed or not.
   *
   * @param $dsid
   *   Data stream identifier.
   *
   * @return
   *  TRUE if the data stream specified by $disd is managed, FALSE otherwise.
   */
  private function isDatastreamManaged($pid, $dsid) {
    $fedora_item = new Fedora_Item($pid);
    $datastream = $fedora_item->get_datastream($dsid);
    if ($datastream) {
      return $datastream->controlGroup == 'M';
    }
    else {
      return FALSE;
    }
  }

  /**
   * Overwrites a managed data stream by reference.
   *
   * Only works for managed data streams. Creates a temp file to be used as the
   * reference
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyManagedDatastream(DOMDocument $dom, $pid, $dsid, $label, $mime_type) {
    $temp_file_dir = file_directory_path();
    $temp_file_path = file_create_filename("datastream.xml", $temp_file_dir);
    if ($dom->save($temp_file_path)) {
      $temp_file_url = file_create_url($temp_file_path);
      $fedora_item = new Fedora_Item($pid);
      if ($fedora_item->modify_datastream_by_reference($temp_file_url, $dsid, $label, $mime_type) !== NULL) {
        drupal_set_message(t('Successfully updated %dsid datastream for object %pid', array('%pid' => $pid, '%dsid' => $dsid)));
      }
    }
    file_delete($temp_file_path);
  }

  /**
   * Overwrites a inlined data stream by value.
   *
   * Only works for inlined data streams.
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyInlineDatastream(DOMDocument $dom, $pid, $dsid, $label, $mime_type) {
    $fedora_item = new Fedora_Item($pid);
    if (!array_key_exists($dsid, $fedora_item->datastreams)) {
      $xml = $dom->saveXML();
      $fedora_item->add_datastream_from_string($dom->saveXML(), $dsid, $label, $mime_type, 'X');
      return;
    }
    if ($fedora_item->modify_datastream_by_value($dom->saveXML(), $dsid, $label, $mime_type) !== NULL) {
      drupal_set_message(t('Successfully updated %dsid datastream for object %pid', array('%pid' => $pid, '%dsid' => $dsid)));
    }
  }

  /**
   * Overwrites a data stream.
   *
   * Overwrites the given datastream by value for inline datastreams and by
   * reference for managed datastreams.
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyDatastream($dom, $pid, $dsid, $label, $mime_type) {

    if ($this->isDatastreamManaged($pid, $dsid)) {
      $this->modifyManagedDatastream($dom, $pid, $dsid, $label, $mime_type);
    }
    else {
      $this->modifyInlineDatastream($dom, $pid, $dsid, $label, $mime_type);
    }
  }

}
