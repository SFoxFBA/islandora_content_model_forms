<?php

/**
 * @file
 * @author Nicholas Bywell of the Freshwater Biological Association (FBA).
 *
 * This file contains most of the functions that handle Object Linking
 * in XML-Form functionality.
 * It uses some of the ontology processing functionality developed by
 * Giancoralo Birello of CNR-Ceris and Rosemary Le Faive of McGill University
 * in the Islandora Relationship Editor module.
 */

/**
 * Searches for data in the datastream provided which matches
 * the Object Linking info relating to one or more RDF-autocomplete
 * entries associated with the XML-form that have been configured
 * in the sparql submodule.
 * It strips out any object-PIDs so that they are not stored in
 * the datastream and stores the relevant info in the 'rdf_relationships'
 * variable.
 *
 * @param DOMDocument $definition
 *   The Datastream XML that may contain data to trigger RDF linking.
 * @return
 *   None
 */
function preprocess_link_data(DOMDocument &$document) {
  $namespacePrefixesRegistered = array();
  $checkPreviousNamespacePrefixes = true;
  $pidDelim = '||';
  $rdfRelationshipCount = 0;
  $charsInDelim = 2;
  $rdf_relationships = variable_get('rdf_relationships', NULL);
  $domxpath = new domXpath($document);
  foreach ($rdf_relationships as $rdf_relationship) {
    $subjectXpathDataSearch = $rdf_relationship['subject_xpath_data_search'];
    if (strlen($subjectXpathDataSearch) > 0) {
      register_xpath_namespaces($domxpath, $rdf_relationship['subject_namespace_info'], $checkPreviousNamespacePrefixes, $namespacePrefixesRegistered);
      $queryResults = $domxpath->query($subjectXpathDataSearch);
      $queryResultCount = 0;
      if ($queryResults->length > 0) {
        foreach ($queryResults as $queryResult) {
          $objectPidFound = 0;
          $objectItem = $queryResult->textContent;
          if ($rdf_relationship['object_pid_append_required']) {
            $objectPid = strstr($objectItem, $pidDelim);
            if (strlen($objectPid) > $charsInDelim) {
              $objectPid = substr($objectPid, $charsInDelim);
              $delimPos = strpos($objectPid, $pidDelim);
              if ($delimPos !== false) {
                $objectPid = substr($objectPid, 0, $delimPos);
                if (strlen($objectPid) > 0) {
                  $objectPidFound = 1;
                  $dataMinusPid = substr($objectItem, 0, strlen($objectItem) - (strlen($objectPid) + (2 * $charsInDelim) + 1));
                  $rdf_relationships[$rdfRelationshipCount]['rdf_link'][]['pid'] = $objectPid;
                  $rdf_relationships[$rdfRelationshipCount]['rdf_link'][$queryResultCount]['data'] = $dataMinusPid;
                  $queryResult->nodeValue = $dataMinusPid;
                }
              }
            }
          }
          if (!$objectPidFound) {
            $rdf_relationships[$rdfRelationshipCount]['rdf_link'][]['pid'] = 'nopid';
            $rdf_relationships[$rdfRelationshipCount]['rdf_link'][$queryResultCount]['data'] = $objectItem;
          }
          $queryResultCount+=1;
        }
      }
      $rdfRelationshipCount+=1;
    }
  }
  variable_set('rdf_relationships', $rdf_relationships);
}

/**
 * Processes the configuration info in the 'rdf_relationships' variable,
 * which relates to one or more RDF-autocomplete entries in the MySQL
 * database that are associated with the XML-Form.
 * RDF-links and inverse-links are created/removed from subject and object
 * where appropriate. The relevant datastreams in the objects are updated
 * with symmetrical entries, where appropriate.
 *
 * @param array $form
 *   The data relating to the XML-form that has been submitted.
 * @param array $form_state
 *   Contains the PID of the object that possesses the datastream
 *   that populated the XML-Form.
 * @param boolean $editFlag
 *   Indicates whether processing the ingest of an object (FALSE)
 *   or the edit of an existing object (TRUE)
 * @return
 *   None
 */
function process_links(&$form, &$form_state, $editFlag) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'islandora_autocomplete_sparql', 'includes/islandora_autocomplete_sparql.vocab');

  $replaceSubjectTitleInObjectDatastream = false;
  $preEditSubjectTitle = '';
  $postEditSubjectTitle = '';
  $linkTypeOutwardOnly = 0;
  $linkTypeOutwardAndInverse = 1;
  $linkTypeInverseOnly = 2;
  global $user;

  // Check whether any autocomplete entries that involve rdf-linking
  // were found while the xml-form was being built.  
  if (variable_get('rdf_relationships', NULL)) {
    $rdf_relationships = variable_get('rdf_relationships', NULL);
    variable_set('rdf_relationships', NULL);

    $subjectPid = trim($form_state[values][pid]);
    $subject = islandora_object_load($subjectPid);

    if ($subject) {
      // Get the dc.title value for the subject-object.
      // It would be preferable to call the generic
      // function get_datastream_content_via_xpath()
      // in the fba_utility module, but this would
      // involve a second call to islandora_object_load
      // which would be an unnecessary performance hit,
      // so a local version of the function is used instead.
      $subjectDCTitle = get_datastream_content_via_xpath_local($subject, 'DC', PURL_ELEMENTS_URI, '/oai_dc:dc/dc:title');
      $rdfRelationshipCount = 0;

      // For dataset processing, construct the user collection.
      // This is effectively ignored for non-dataset processing.
      $userId = $user->islandora_user_pid;
      if (strlen($userId) > 0) {
        $userCollectionNs = str_replace(array('-', ':'), '', $userId);
        $userCollection = $userCollectionNs . ':collection';
      }

      // Set up the batch array for ingest and update batch processing.
      $progressMessage = 'Processed @current out of @total';
      $batch = array(
        'title' => t('Processing ...'),
        'file' => drupal_get_path('module', 'islandora_content_model_forms') . '/ObjectLinker.inc',
        'operations' => array(),
        'finished' => 'completeObjectLinkerBatchProcessing',
        'init_message' => t('Initializing... '),
        'progress_message' => t($progressMessage),
        'error_message' => t('An error was encountered while processing links.'),
      );

      // Process through the relationship-configuration entries
      // associated with the xml-form and take the appropriate
      // action.
      foreach ($rdf_relationships as $rdf_relationship) {
        list($rdfNamespace, $rdfName) = fba_rdfx_split_uri($rdf_relationship['rdf_predicate']);
        $rdfRelationshipCount += 1;
        if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse || $rdf_relationship['link_type'] == $linkTypeInverseOnly) {
          $inversePredicate = fba_textfield_inverseof_predicate($rdf_relationship['rdf_predicate']);
          $inverseError = false;
          if (strcmp($inversePredicate, 'No inverse predicate')) {
            list($inverseNamespace, $inverseName) = fba_rdfx_split_uri($inversePredicate);
            if (strlen($inverseName) == 0) {
              $inverseError = true;
            }
          }
          else {
            $inverseError = true;
          }
          if ($inverseError) {
            if ($editFlag) {
              $msgInsert = $postEditSubjectTitle;
            }
            else {
              $msgInsert = $subjectDCTitle;
            }
            $msg = 'While creating or editing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create an inverse of the "' . $rdfName . '" link, the inverse was not found.';
            drupal_set_message(t($msg));
            break;
          }
        }
        if ($editFlag) {
          if ($rdfRelationshipCount == 1) {
            // Get the pre and post edit title values of
            // the subject-object. Only needs to be done once.
            if (strlen($rdf_relationship['subject_xpath_title_search']) > 0) {
              $titleArray = get_pre_and_post_edit_subject_titles_from_form($form, $rdf_relationship['subject_xpath_title_search']);
              if (count($titleArray) == 2) {
                $preEditSubjectTitle = $titleArray[0];
                $postEditSubjectTitle = $titleArray[1];
                // If the title has been modified, the new title should
                // replace the old title in the object-object where any
                // existing datastream symmetry is involved for this
                // relationship or any other relationship associated with
                // this form.
                if (strlen($preEditSubjectTitle) > 0) {
                  if (strcmp($postEditSubjectTitle, $preEditSubjectTitle)) {
                    $replaceSubjectTitleInObjectDatastream = true;
                  }
                }
              }
              else {
                $msg = 'While processing "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" the path corresponding to the "Subject XPath Title Search" field in the autocomplete record for the "' . $rdfName . '" link was not found.';
                drupal_set_message(t($msg));
              }
            }
            else {
              $msg = 'While processing "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" the "Subject Title XPath Search" field in the autocomplete recordfor the "' . $rdfName . '" link was found to be empty.';
              drupal_set_message(t($msg));
            }
          }
          // Store all the existing RDFs relating to this relationship
          // in an array.
          if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
            $spoSubject = '<info:fedora/' . $subjectPid . '>';
            $spoPredicate = '<' . $rdf_relationship['rdf_predicate'] . '>';
            $spoObject = '*';
          }
          else {
            // It must be an inverse-only type of link.
            $spoSubject = '*';
            $spoPredicate = '<' . $inversePredicate . '>';
            $spoObject = '<info:fedora/' . $subjectPid . '>';
          }
          $preEditObjectTriplesForRelationship = spo_search($spoSubject, $spoPredicate, $spoObject);
        }
        $rdfRelationshipRdfLinkCount = 0;
        $rdfRelationshipRdfLinkTot = count($rdf_relationship['rdf_link']);
        if ($rdfRelationshipRdfLinkTot > 0) {
          process_instances_of_this_relationship($batch, $rdf_relationship, $editFlag, $userCollection, $preEditObjectTriplesForRelationship, $replaceSubjectTitleInObjectDatastream, $subjectPid, $subject, $subjectDCTitle, $preEditSubjectTitle, $postEditSubjectTitle, $rdfName, $inverseName, $rdfNamespace, $inversePredicate, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly);
        }
        if ($editFlag) {
          if ($preEditObjectTriplesForRelationship) {
            foreach ($preEditObjectTriplesForRelationship as $preEditObjectTripleForRelationship) {
              $batch['operations'][] = array('process_edit_batch', array($rdf_relationship, $rdfRelationshipRdfLinkTot, $subjectPid, $rdfName, $rdfNamespace, $inverseName, $inversePredicate, $preEditObjectTripleForRelationship, $replaceSubjectTitleInObjectDatastream, $preEditSubjectTitle, $postEditSubjectTitle, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly));
            }
          }
        }
      }
      batch_set($batch);
      // The next three lines are only necessary in the dataset
      // processing environment. They turn off the progress-bar
      // display because otherwise the url redirect is caught
      // by the dataset-processing and stops the functionality
      // from working.
      $batch = &batch_get();
      $batch['progressive'] = FALSE;
      batch_process();
    }
    else {
      $msg = 'While processing the linking for a record with ID "' . $subjectPid . '" the subject-object failed to load.';
      drupal_set_message(t($msg));
    }
  }
}

/**
 * Processes RDF-links and datastream modifications corresponding
 * to specific entries in the subject-object datastream.
 *
 * @param array $batch
 *   The array that controls batch processing
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param boolean $editFlag
 *   Indicates whether the subject-object is being edited.
 * @param string  $userCollection
 *   The identifier for the user in dataset processing. Not
 *   used for non-dataset processing.
 * @param array   $preEditObjectTriplesForRelationship
 *   The RDF links that were present previous to the user edit.
 * @param boolean $replaceSubjectTitleInObjectDatastream
 *   Flag to indicate that a change to the subject-object title
 *   necessitates an update to that string where it appears
 *   in an object-object datastream.
 * @param string $subjectPid
 *   The Persistent Identifier of the subject-object in the RDF
 * @param array  $subject
 *   The loaded subject-object
 * @param string $subjectDCTitle
 *   Contains the title of the subject for a newly ingested
 *   subject-object
 * @param string $preEditSubjectTitle
 *   If the user has edited an existing subject-object this
 *   contains the title of the subject-object prior
 *   to the XML form having been edited by the user.
 * @param string $postEditSubjectTitle
 *   If the user has edited an existing subject-object this
 *   contains the title of the subject object after
 *   the XML form has been edited by the user.
 * @param string $rdfName
 *   The verb of the predicate component in the RDF
 * @param string $inverseName
 *   The verb of the predicate component in the inverse RDF
 * @param string $rdfNamespace
 *   The stem of the predicate component in the RDF
 * @param string $inversePredicate
 *   The inverse of the predicate associated with the subject-object
 * @param int $linkTypeOutwardOnly
 *   Constant for link type that is subject to object only
 * @param int $linkTypeOutwardAndInverse
 *   Constant for link type that is subject to object and object to subject
 * @param int $linkTypeInverseOnly
 *   Constant for link type that is object to subject only
 * return
 *   None
 */
function process_instances_of_this_relationship(&$batch, &$rdf_relationship, &$editFlag, &$userCollection, &$preEditObjectTriplesForRelationship, &$replaceSubjectTitleInObjectDatastream, &$subjectPid, &$subject, &$subjectDCTitle, &$preEditSubjectTitle, &$postEditSubjectTitle, &$rdfName, &$inverseName, &$rdfNamespace, &$inversePredicate, &$linkTypeOutwardOnly, &$linkTypeOutwardAndInverse, &$linkTypeInverseOnly) {

  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');

  $userCollectionPlaceHolder = 'user_collection_place_holder';
  $searchTermPlaceHolder = 'search_term_place_holder';

  foreach ($rdf_relationship['rdf_link'] as $link) {
    // If the user selected data from the autocomplete list,
    // and object_pid_append_required is set, there should be an
    // object-pid associated with it. However, it may not have been
    // set or, even if it was set, the user may have typed in some data
    // and this data might correspond to the title of an existing
    // object, so check if such an object exists and obtain its
    // PID if it does, so that an RDF link can be created to it.
    // If no existing object exists, but the autocomplete has been
    // set up such that the object should be created in such a
    // scenario, then create the object so that an RDF link
    // from the subject-object can be created to it.
    if (!strcmp($link['pid'], 'nopid') && strlen($link['data']) > 0) {
      // Construct a sparql query to check for the exitence of
      // the object.
      if ($rdf_relationship['object_creation_required']) {
        // As object creation is required, the sparql query is
        // constructed from the criteria specified in the relevant 
        // fields of the autocomplete record, e.g. the Object
        // Content Model and Collection Pids.
        $sparqlTestObjectExistence = sprintf('SELECT ?title ?object WHERE { ?object <%stitle> ?title; <fedora-model:hasModel> <info:fedora/%s>; <fedora-rels-ext:isMemberOfCollection> <info:fedora/%s> FILTER (regex(str(?title), "^%s$", "i"))}', PURL_ELEMENTS_URI, $rdf_relationship['object_content_model_pid'], $rdf_relationship['object_collection_pid'], escape_for_sparql($link['data']));
      }
      else {
        // As object creation is not required, the sparql query
        // that is used for the auto-complete is used instead
        // as this allows for greater flexibility.
        $sparqlTestObjectExistence = $rdf_relationship['sparql_field'];

        if (strpos($sparqlTestObjectExistence, $searchTermPlaceHolder)) {
          // Substitute the data for the search term place holder and
          // add a dollar sign so that an exact match is specified.
          $linkDataPlusDollar = escape_for_sparql($link['data']) . '$';
          $sparqlTestObjectExistence = str_replace($searchTermPlaceHolder, $linkDataPlusDollar, $sparqlTestObjectExistence);
        }
      }

      // For dataset processing, replace the user-collection-place-holder
      // with the actual user collection.
      if (strlen($userCollection) > 0 && strpos($sparqlTestObjectExistence, $userCollectionPlaceHolder)) {
        $userCollectionFullPid = 'info:fedora/' . $userCollection;
        $sparqlTestObjectExistence = str_replace($userCollectionPlaceHolder, $userCollectionFullPid, $sparqlTestObjectExistence);
      }
      $limit = -1;
      $offset = 0;
      $objectExistenceQueryResults = ObjectHelper::performRiQuery($sparqlTestObjectExistence, 'sparql', $limit, $offset);
      $objectExistenceCount = 0;
      foreach ($objectExistenceQueryResults as $objectExistenceQueryResult) {
        $objectExistenceTitle = $objectExistenceQueryResult['title'];
        $objectExistencePid = $objectExistenceQueryResult['object'];
        $objectExistenceCount += 1;
      }

      if ($objectExistenceCount == 0) {
        if ($rdf_relationship['object_creation_required']) {
          $newObjectNamespacePrefix = $rdf_relationship['object_namespace_prefix'];
          $newObjectPid = fedora_item::get_next_PID_in_namespace($newObjectNamespacePrefix);
          $link['pid'] = $newObjectPid;
          $rdf_relationship['rdf_link'][$rdfRelationshipRdfLinkCount][pid] = $newObjectPid;
          $batch['operations'][] = array('add_object_batch', array($newObjectPid, $rdf_relationship, $subjectPid, $link['data'], $editFlag, $postEditSubjectTitle, $subjectDCTitle, $link['data']));
        }
      }
      else if ($objectExistenceCount == 1) {
        $link['pid'] = $objectExistencePid;
        $rdf_relationship['rdf_link'][$rdfRelationshipRdfLinkCount][pid] = $objectExistencePid;

        if ($editFlag && $rdf_relationship['symmetry_in_object_datastream_required'] && $replaceSubjectTitleInObjectDatastream) {
          // The editor has changed the title of the subject-object
          // so this must be reflected in the object-object
          // datastream that contains it.
          if ($preEditObjectTriplesForRelationship) {
            $preEditMatchFound = false;
            foreach ($preEditObjectTriplesForRelationship as $preEditObjectTripleForRelationship) {
              if (!strcmp($preEditObjectTripleForRelationship['objectPid'], $objectExistencePid)) {
                $preEditMatchFound = true;
              }
            }
            if ($preEditMatchFound) {
              $batch['operations'][] = array('replace_text_in_datastream_batch', array($rdf_relationship, $objectExistencePid, $preEditSubjectTitle, $postEditSubjectTitle));
            }
          }
        }
      }
      else {
        if ($editFlag) {
          $msgInsert = $postEditSubjectTitle;
        }
        else {
          $msgInsert = $subjectDCTitle;
        }
        $msg = 'While processing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create a "' . $rdfName . '" link to "' . $link['data'] . '" more than one record was found with the same title, so the link was not created.';
        //drupal_set_message(t($msg));
      }
    }
    // If an object-pid exists create the appropriate RDF links.
    if (strcmp($link['pid'], 'nopid')) {
      $batch['operations'][] = array('create_rdf_links_batch', array($rdf_relationship, $subjectPid, $link['pid'], $rdfName, $rdfNamespace, $inverseName, $inversePredicate, $editFlag, $subjectDCTitle, $postEditSubjectTitle, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly));
    }
    $rdfRelationshipRdfLinkCount += 1;
  }
}

/**
 * Retrieve the pre-edit title and post-edit
 * title from the form array.
 * Serializing the entire form-array and then
 * doing a string search for the relevant fields
 * is very ugly. Unfortunately there does not
 * seem to be any other way of obtaining this
 * data because it is impossible to search a
 * multidimensional array with a string, even
 * if the string exactly mimics the equivalent
 * PHP statement that would access the desired
 * variables.

 * @param array $form
     The array containing the XML-Form data.
 * @param string $subjectXpathTitleSearch
 *   The Xpath string that identifies the 
 *   title of the subject-object
 * @return
 *   Array containing pre and post edit titles
 *   or NULL
 */
function get_pre_and_post_edit_subject_titles_from_form(&$form, $subjectXpathTitleSearch) {
  $title = array();
  $preEditTitle = '';
  $postEditTitle = '';
  $preEditTitleFieldLabel = "#default_value";
  $postEditTitleFieldLabel = "#value";
  $startFieldDelim = '"';
  $endFieldDelim = '";s'; // If it were just the double-quote, it would not process titles that contained quotes correctly.
  $serializedForm = serialize($form);
  $xpathMatched = strstr($serializedForm, $subjectXpathTitleSearch);
  if ($xpathMatched) {
    $preEditTitleFieldLabelMatched = strstr($xpathMatched, $preEditTitleFieldLabel);
    if ($preEditTitleFieldLabelMatched) {
      $afterPreEditFieldLabel = substr($preEditTitleFieldLabelMatched, (strlen($preEditTitleFieldLabel) + 1));
      $startPreEditTitleFieldDelim = strstr($afterPreEditFieldLabel, $startFieldDelim);
      if ($startPreEditTitleFieldDelim) {
        $endPreEditTitleFieldDelimPos = strpos($startPreEditTitleFieldDelim, $endFieldDelim, 1);
        if ($endPreEditTitleFieldDelimPos > 0) {
          $endPreEditTitleFieldPos = $endPreEditTitleFieldDelimPos - 1;
          $preEditTitle = substr($startPreEditTitleFieldDelim, 1, $endPreEditTitleFieldPos);
        }
        $postEditTitleFieldLabelMatched = strstr($startPreEditTitleFieldDelim, $postEditTitleFieldLabel);
        if ($postEditTitleFieldLabelMatched) {
          $afterPostEditFieldLabel = substr($postEditTitleFieldLabelMatched, (strlen($postEditTitleFieldLabel) + 1));
          $startPostEditTitleFieldDelim = strstr($afterPostEditFieldLabel, $startFieldDelim);
          if ($startPostEditTitleFieldDelim) {
            $endPostEditTitleFieldDelimPos = strpos($startPostEditTitleFieldDelim, $endFieldDelim, 1);
            if ($endPostEditTitleFieldDelimPos > 0) {
              $endPostEditTitleFieldPos = $endPostEditTitleFieldDelimPos - 1;
              $postEditTitle = substr($startPostEditTitleFieldDelim, 1, $endPostEditTitleFieldPos);
            }
          }
        }
      }
    }
  }

  if (strlen($preEditTitle) > 0 && strlen($postEditTitle) > 0) {
    $title[] = $preEditTitle;
    $title[] = $postEditTitle;
    return $title;
  }
  else {
    return NULL;
  }
}

/**
 * Escapes various characters that would otherwise
 * cause a SPARQL query to fail.
 *
 * @param string $inputString
 *   The string in which the escaping is to be performed.
 * return string $inputString
 *   A string with the characters escaped.
 */
function escape_for_sparql($inputString) {
  if (strlen($inputString) > 0) {
    $inputString = str_replace('(', '\\\(', $inputString);
    $inputString = str_replace(')', '\\\)', $inputString);
    $inputString = str_replace('{', '\\\{', $inputString);
    $inputString = str_replace('}', '\\\{', $inputString);
    $inputString = str_replace('+', '\\\+', $inputString);
    $inputString = str_replace('?', '\\\?', $inputString);
    $inputString = str_replace('^', '\\\^', $inputString);
    $inputString = str_replace('*', '\\\*', $inputString);
    $inputString = str_replace('.', '\\\.', $inputString);
    $inputString = str_replace('"', '\\\"', $inputString);
    $inputString = str_replace("'", "\\\'", $inputString);
    return $inputString;
  }
  else {
    return '';
  }
}

/**
 * Performs an spo query of the triple-store and returns an array
 * of object PIDS derived from the query results.
 *
 * @param string $subject
 *   The subject component of the required spo query
 * @param string $predicate
 *   The predicate component of the required spo query
 * @param string $object
 *   The object component of the required spo query
 * return
 *   Array of triples returned by the spo query
 *   or NULL
 */
function spo_search($subject = '*', $predicate = '*', $object = '*') {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $query = $subject . ' ' . $predicate . ' ' . $object;
  $offset = 0;
  $limit = 10000;

  $encodedquery = htmlentities(urlencode($query));

  $url = variable_get('fedora_repository_url', 'http://localhost:8080/fedora/risearch');
  $url .= "?type=triples&flush=TRUE&format=N-Triples&limit=$limit&offset=$offset&lang=spo&stream=on&query=" . $encodedquery;
  $content = trim(do_curl($url));

  if ($content) {
    $content = str_replace("\n", '', $content);
    $triples = explode("> .", $content);
    $tripleCount = count($triples) - 1;
    $subjectElement = 0;
    $predicateElement = 1;
    $objectElement = 2;
    $processedTriples = array();
    for ($i = 0; $i < $tripleCount; $i++) {
      $elements = explode("> ", $triples[$i]);
      $namespacedSubjectPid = substr($elements[$subjectElement], strlen('<info:fedora/'));
      $requiredPredicateElementLen = strlen($elements[$predicateElement]) - 1;
      $predicateOfTriple = substr($elements[$predicateElement], 1, $requiredPredicateElementLen);
      $namespacedObjectPid = substr($elements[$objectElement], strlen('<info:fedora/'));
      $processedTriples[] = array("subjectPid" => $namespacedSubjectPid, "predicate" => $predicateOfTriple, "objectPid" => $namespacedObjectPid);
    }
    return $processedTriples;
  }
  else {
    return NULL;
  }
}

/**
 * Search for a match for a specific node element content in the
 * specified datastream and object. If there is more than one such
 * node-group present, remove the entire node. Otherwise, just
 * remove the content of the node element. Update the datastream
 * of the actual object.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $objectPid
 *   The PID of the object in which the datastream resides.
 * @param string $objectXpathSearch
 *   The XPath statement that searches the datastream
 * @param string $objectXpathBuildCountback
 *   The number of nodes, counting backwards that should
 *   be removed, when group-node removal is required.
 * @param string $elementContent
 *   The node element to be matched
 * return
 *   None
 */
function remove_from_datastream(&$rdf_relationship, $objectPid, $objectXpathSearch, $objectXpathBuildCountback, $elementContent) {

  $objectDatastreamID = $rdf_relationship['object_dsid'];
  $object = islandora_object_load($objectPid);
  $objectDatastream = $object[$objectDatastreamID];
  $objectDatastreamXml = $objectDatastream->content;
  $objectDoc = new domDocument();
  $objectDoc->loadXML($objectDatastreamXml);
  $objectXpath = new domXpath($objectDoc);
  register_xpath_namespaces($objectXpath, $rdf_relationship['object_namespace_info']);
  $removeNodeQueryResults = $objectXpath->query($objectXpathSearch);
  $removeNodeQueryResultsTot = $removeNodeQueryResults->length;
  if ($removeNodeQueryResultsTot > 0) {
    foreach ($removeNodeQueryResults as $node) {
      if (!strcmp($node->nodeValue, $elementContent)) {
        if ($removeNodeQueryResultsTot == 1) {
          $node->nodeValue = '';
        }
        else {
          for ($x = 1; $x <= $objectXpathBuildCountback; $x++) {
            $childNode = $node;
            $parentNode = $childNode->parentNode;
            $node = $parentNode;
          }
          $parentNode->removeChild($childNode);
        }
      }
    }
    // Update the datastream with the changed xml.
    $updatedObjectDatastreamXml = $objectDoc->saveXML();
    $objectDatastream->content = $updatedObjectDatastreamXml;
  }
}

/**
 * Search for a match for a specific node element content in the
 * specified datastream and object. Replace the content of the
 * of the first node that matches the search with the string provided.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $objectPid
 *   The PID of the object in which the datastream resides.
 * @param string $objectXpathSearch
 *   The XPath statement that searches the datastream
 * @param string $elementContent
 *   The node element value to be matched and replaced
 * @param string $replacementContent
 *   The node element value that replaces the matched value
 * return
 *   None
 */
function replace_text_in_datastream(&$rdf_relationship, $objectPid, $objectXpathSearch, $elementContent, $replacementContent) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');

  $objectDatastreamID = $rdf_relationship['object_dsid'];
  $object = islandora_object_load($objectPid);
  $objectDatastream = $object[$objectDatastreamID];
  $objectDatastreamXml = $objectDatastream->content;
  $objectDoc = new domDocument();
  $objectDoc->loadXML($objectDatastreamXml);
  $objectXpath = new domXpath($objectDoc);
  register_xpath_namespaces($objectXpath, $rdf_relationship['object_namespace_info']);
  $replaceNodeQueryResults = $objectXpath->query($objectXpathSearch);
  $replaceNodeQueryResultsTot = $replaceNodeQueryResults->length;
  if ($replaceNodeQueryResultsTot > 0) {
    foreach ($replaceNodeQueryResults as $node) {
      if (!strcmp($node->nodeValue, $elementContent)) {
        $node->nodeValue = $replacementContent;
        break;
      }
    }
    // Update the datastream with the changed xml.
    $updatedObjectDatastreamXml = $objectDoc->saveXML();
    $objectDatastream->content = $updatedObjectDatastreamXml;
  }
}

/**
 *
 * Search for a match for a specific node element content in the
 * specified datastream. If it is already present, do nothing.
 * If an empty node is present, add the content to it. Otherwise,
 * add the required node-group with the content set.
 * The datastream of the object is not updated within this
 * function because the object may not exist at the point at
 * which this function is called.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $objectDatastreamXml
 *   The datastream to which the data is added.
 * @param string $objectXpathSearch
 *   The XPath statement that searches the datastream
 * @param string $objectXpathBulid
 *   The pseudo-XPath that provides a template to build
 *   the XML for adding a node-group when necessary.
 * @param string $objectXpathBuildCountback
 *   The number of nodes, counting backwards that should
 *   be added, when group-node addition is required.
 * @param string $elementContent
 *   The node element content to be added.
 * return
 *   true or false to indicate error
 */
function add_to_datastream(&$rdf_relationship, &$objectDatastreamXml, $objectXpathSearch, $objectXpathBuild, $objectXpathCountback, $elementContent) {
  $returnValue = true;
  $objectDoc = new domDocument();
  $objectDoc->loadXML($objectDatastreamXml);
  $objectXpath = new domXpath($objectDoc);
  register_xpath_namespaces($objectXpath, $rdf_relationship['object_namespace_info']);
  $objectQueryResults = $objectXpath->query($objectXpathSearch);

  $objectQueryResultsTot = $objectQueryResults->length;
  $elementContentAddedToObjectDS = false;
  $elementContentAlreadyPresent = false;
  // Check whether the relevant xml node is present.
  if ($objectQueryResultsTot > 0) {
    foreach ($objectQueryResults as $objectQueryResult) {
      $objectItem = trim($objectQueryResult->textContent);
      // Check if the specific element content is present (case insensitive).
      if (!strcasecmp($objectItem, $elementContent)) {
        $elementContentAlreadyPresent = true;
        break;
        // Check if there is an empty element available.
      }
      else if (strlen($objectItem) == 0) {
        if (!$elementContentAddedToObjectDS) {
          $objectQueryResult->nodeValue = $elementContent;
          // Update the datastream with the xml containing the
          // updated node.
          $objectDatastreamXml = $objectDoc->saveXML();
          $elementContentAddedToObjectDS = true;
          break;
        }
      }
    }
  }
  if (!$elementContentAddedToObjectDS && !$elementContentAlreadyPresent) {
    // Add a new node containing the element content.
    $objectXpathComponents = explode('/', $objectXpathBuild);
    $objectXpathComponentsTot = count($objectXpathComponents);
    $startObjectXpathComponent = $objectXpathComponentsTot - $objectXpathCountback;
    $nodePref = 'node';
    for ($objectXpathComponentCount = $objectXpathComponentsTot; $objectXpathComponentCount > $startObjectXpathComponent; $objectXpathComponentCount--) {
      $elementDefinition = $objectXpathComponents[$objectXpathComponentCount - 1];
      $attributePhraseStart = strpos($elementDefinition, '[');
      $attributePhrase = strstr($elementDefinition, '[');
      if (strlen($attributePhraseStart) == 0) {
        ${$nodePref . $objectXpathComponentCount} = $objectDoc->createElement($elementDefinition);
      }
      else {
        $elementName = substr($elementDefinition, 0, $attributePhraseStart);
        ${$nodePref . $objectXpathComponentCount} = $objectDoc->createElement($elementName);
        $attributePhrase = substr($attributePhrase, 1);
        $delimPos = strpos($attributePhrase, ']');
        if ($delimPos !== false) {
          $attributePhrase = substr($attributePhrase, 0, $delimPos);
          if (strlen($attributePhrase) > 0) {
            $attributeAndPresent = strpos($attributePhrase, ' and ');
            if ($attributeAndPresent > 0) {
              $attributes = explode(' and ', $attributePhrase);
            }
            else {
              $attributes = array();
              $attributes[0] = $attributePhrase;
            }
            $attributesTot = count($attributes);
            // Attach any attributes that are present to the
            // current node.
            for ($x = 0; $x < $attributesTot; $x++) {
              $attribute = explode('=', $attributes[$x]);
              $attribute[0] = trim($attribute[0]);
              if (strlen($attribute[0]) > 0) {
                $attributeTerm = substr($attribute[0], 1);
                $domAttribute = $objectDoc->createAttribute($attributeTerm);
                if (strlen($attribute[1]) > 2) {
                  $attribute[1] = trim($attribute[1]);
                  $attributeValue = substr($attribute[1], 1, strlen($attribute[1]) - 2); // Strip off quotes.
                  $domAttribute->value = $attributeValue;
                  ${$nodePref . $objectXpathComponentCount}->appendChild($domAttribute);
                }
              }
            }
          }
        }
      }
      if ($objectXpathComponentCount == $objectXpathComponentsTot) {
        $whatIsThis = $objectXpathComponents[$objectXpathComponentCount - 1];
        ${$nodePref . $objectXpathComponentCount}->appendChild($objectDoc->createTextNode($elementContent));
      }
      else {
        $prevObjectXpathComponentCount = $objectXpathComponentCount + 1; //Incrementing by one here rather than within the appendChild function is necessary for the values to concatenate correctly for the variable-variable name.
        ${$nodePref . $objectXpathComponentCount}->appendChild(${$nodePref . $prevObjectXpathComponentCount});
      }
    }

    $objectXpathStem = $objectXpathSearch;
    for ($y = 0; $y < $objectXpathCountback; $y++) {
      $objectXpathStemPos = strrpos($objectXpathStem, '/');
      $objectXpathStem = substr($objectXpathStem, 0, $objectXpathStemPos);
    }
    $queryResults = $objectXpath->query($objectXpathStem);
    $queryResultsTot = $queryResults->length;
    // Loop through results in order to append node at the correct point.
    if ($queryResultsTot > 0) {
      foreach ($queryResults as $queryResult) {
        $dataReport = $queryResult->textContent;
      }
    }
    // Append the node.
    $prevObjectXpathComponentCount = $objectXpathComponentCount + 1;
    if ($queryResultsTot > 0) {
      $queryResult->appendChild(${$nodePref . $prevObjectXpathComponentCount});

      // Update the datastream with the xml containing the added node.
      $objectDatastreamXml = $objectDoc->saveXML();
    }
    else {
      $returnValue = false;
    }
  }
  return $returnValue;
}

/**
 *
 * Obtain the content-model-id of the object in order to
 * get the XML-Form and build the XML-Form to determine
 * whether there are any sparql-autocompletes attached.
 * Remove any links corresponding to this object that are
 * associated with other objects.
 *
 * @param string $pid 
 *   The PID of the object to be purged.
 * return
 *   None
 */
function process_links_on_purge($pid) {
  if (variable_get('rdf_relationships', NULL)) {
    // An unrelated rdf_relationships variable
    // is present that should be unset. This can happen
    // when a user loads up an XML-form but never clicks
    // on the submit button.
    variable_set('rdf_relationships', NULL);
  }
  $content_model_pid = get_object_linker_content_model($pid);
  if (strlen($content_model_pid) > 0) {
    $object_linker_form_name = get_object_linker_form_name($pid, $content_model_pid);
    if (strlen($object_linker_form_name) > 0) {
      // Build the form so that any link info
      // is stored in the rdf_relationships
      // variable.
      build_object_linker_form($object_linker_form_name);
      if (variable_get('rdf_relationships', NULL)) {
        // Set up the batch array for link removal batch processing.
        $progressMessage = 'Processed @current out of @total';
        $batch = array(
          'title' => t('Processing ...'),
          'file' => drupal_get_path('module', 'islandora_content_model_forms') . '/ObjectLinker.inc',
          'operations' => array(),
          'finished' => 'completeObjectLinkerBatchProcessing',
          'init_message' => t('Initializing... '),
          'progress_message' => t($progressMessage),
          'error_message' => t('An error was encountered while removing links.'),
        );
        remove_links_on_purge($batch, $pid);
        batch_set($batch);
        // The next three lines are only necessary in the dataset
        // processing environment. They turn off the progress-bar
        // display because otherwise the url redirect is caught
        // by the dataset-processing and stops the functionality
        // from working.
        $batch = &batch_get();
        $batch['progressive'] = FALSE;
        batch_process();
        variable_set('rdf_relationships', NULL);
      }
    }
  }
}

/**
 *
 * Remove any inverse RDF-links and symmetric entries in the
 * datastreams of linked objects.
 *
 * @param array $batch 
 *   The array that controls the batch processing for link removal
 * @param string $subjectPID 
 *   The PID of the object that is being purged.
 * return
 *   None
 */
function remove_links_on_purge(&$batch, $subjectPid) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'islandora_autocomplete_sparql', 'includes/islandora_autocomplete_sparql.vocab');
  $aggregateTriplesTot = 0;
  $linkTypeOutwardOnly = 0;
  $linkTypeOutwardAndInverse = 1;
  $linkTypeInverseOnly = 2;

  $rdf_relationships = variable_get('rdf_relationships', NULL);
  //variable_set('rdf_relationships', NULL); Do not set this to null here because batch purge reuses this variable when multiple objects have the same form.
  $subject = islandora_object_load($subjectPid);

  if ($subject) {
    //Get the dc.title value for the subject incase it is required
    //for removing inverse symmetric data in the object datastreams.

    $subjectDCTitle = get_datastream_content_via_xpath_local($subject, 'DC', PURL_ELEMENTS_URI, '/oai_dc:dc/dc:title');
    foreach ($rdf_relationships as $rdf_relationship) {
      // The outward rdf links do not need to be removed
      // because they will be deleted from the triple-store 
      // as part of the standard fedora processing on purge.
      if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
        // Store all the RDF-info relating to the outward
        // links for the current relationship in an array
        // so that the inverse links and any symmetric
        // datastream content in the linked-to object can
        // be removed.
        $spoSubject = '<info:fedora/' . $subjectPid . '>';
        $spoPredicate = '<' . $rdf_relationship['rdf_predicate'] . '>';
        $spoObject = '*';
        $objectTriplesForRelationship = spo_search($spoSubject, $spoPredicate, $spoObject);
        $objectTriplesForRelationshipTot = count($objectTriplesForRelationship);
        $aggregateTriplesTot += $objectTriplesForRelationshipTot;
        if ($objectTriplesForRelationshipTot > 0) {
          if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
            $inverseName = fba_textfield_inverseof_value($rdf_relationship['rdf_predicate']);
            if (!strcmp($inverseName, 'No inverse')) {
              $msg = 'While purging "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" and attempting to remove the inverse link for "' . $rdfName . '" on the object "' . $objectPid . '", the inverse linkname was not found in the ontology.';
              drupal_set_message(t($msg));
            }
          }
          list($rdfNamespace, $rdfName) = fba_rdfx_split_uri($rdf_relationship['rdf_predicate']);
          foreach ($objectTriplesForRelationship as $objectTripleForRelationship) {
            $objectPid = $objectTripleForRelationship['objectPid'];
            $batch['operations'][] = array('remove_inverse_rdf_batch', array($subjectPid, $objectPid, $rdf_relationship, $rdfNamespace, $inverseName, $subjectDCTitle));
          }
        }
      }
      else {
        // This must be an inverse-only type of link.
        $inversePredicate = fba_textfield_inverseof_predicate($rdf_relationship['rdf_predicate']);
        $inverseError = false;
        if (strcmp($inversePredicate, 'No inverse predicate')) {
          list($inverseNamespace, $inverseName) = fba_rdfx_split_uri($inversePredicate);
          if (strlen($inverseName) == 0) {
            $inverseError = true;
          }
        }
        else {
          $inverseError = true;
        }
        if ($inverseError) {
          $msgInsert = $subjectDCTitle;
          $msg = 'While creating or editing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create an inverse of the "' . $rdfName . '" link, the inverse was not found.';
          drupal_set_message(t($msg));
          break;
        }

        $spoSubject = '*';
        $spoPredicate = '<' . $inversePredicate . '>';
        $spoObject = '<info:fedora/' . $subjectPid . '>';
        $objectTriplesForRelationship = spo_search($spoSubject, $spoPredicate, $spoObject);
        $objectTriplesForRelationshipTot = count($objectTriplesForRelationship);
        $aggregateTriplesTot += $objectTriplesForRelationshipTot;
        if ($objectTriplesForRelationshipTot > 0) {
          foreach ($objectTriplesForRelationship as $objectTripleForRelationship) {
            $objectPid = $objectTripleForRelationship['subjectPid'];
            $batch['operations'][] = array('remove_inverse_rdf_batch', array($subjectPid, $objectPid, $rdf_relationship, $inverseNamespace, $inverseName, $subjectDCTitle));
          }
        }
      }
    }
    // The processing above should cover normal eventualities, however
    // it is possible that inverse links would remain if, for example,
    // the sparql autocomplete records were modified after objects with
    // related links had been created. The systems administrator should 
    // not have let this situation arise, but we should remove them if
    // they exist. As the links will still exist until the batch
    // operations are executed a check is made to establish whether
    // more links exist than have already been accounted for and the
    // extra batch operation below is only called if there are more.
    // Note that what is referred to as a "subject" within the
    // spo_search and triple, is different from the main "subject" of
    // this purge, because the main "subject" is used as the "object"
    // within the spo_search.
    // The first instance of $rdf_relationships[0] is passed to the
    // function because a parameter and this is as good an arbitrary
    // choice as any because this processing is a catch-all independent
    // of the content of the $rdf_relationships array. If it happens
    // to result in a datastream entry being removed then that is a
    // bonus.
    $spoSubject = '*';
    $spoPredicate = '*';
    $spoObject = '<info:fedora/' . $subjectPid . '>';
    $triples = spo_search($spoSubject, $spoPredicate, $spoObject);
    $triplesTot = count($triples);
    if ($triplesTot > 0 && $triplesTot > $aggregateTriplesTot) {
      foreach ($triples as $triple) {
        list($rdfNamespace, $rdfName) = fba_rdfx_split_uri($triple['predicate']);
        $objectPid = $triple['subjectPid'];
        $batch['operations'][] = array('remove_inverse_rdf_batch', array($subjectPid, $objectPid, $rdf_relationships[0], $rdfNamespace, $rdfName, $subjectDCTitle));
      }
    }
  }
}

/**
 *
 * Get the contents from the datastream specified by the arguments
 * for the object passed in as a parameter. This is a local function
 * that is similar to the get_datastream_content_via_xpath function
 * in GenericFunctions.inc in the fba_utiity module. A modified
 * local version is present here for performance reasons as it takes
 * a parameter of an object that has already been loaded.
 *
 * @param array $object
 *   The object from which the content specified by the xpath
 *   query is to be obtained.
 * @param string $datastremLabel
 *   The label of the datastream that is to be queried
 * @param string $namespace
 *   The namespace to be registered for the xpath query
 * @param string $xpathQuery
 *   The xpath query to identify the required content
 * return string
 *   The required content or NULL
 */
function get_datastream_content_via_xpath_local(&$object, $datastreamLabel, $namespace, $xpathQuery) {
  $datastream = $object[$datastreamLabel];
  $xml = $datastream->content;
  $doc = new domDocument();
  $doc->loadXML($xml);
  $xpath = new domXpath($doc);
  $xpath->registerNamespace(strtolower($datastreamLabel), $xpathQuery);
  $xpathQueryResults = $xpath->query($xpathQuery);

  $xpathQueryResultCount = 0;
  if ($xpathQueryResults->length > 0) {
    foreach ($xpathQueryResults as $xpathQueryResult) {
      $target = $xpathQueryResult->textContent;
      $xpathQueryResultCount += 1;
    }
  }

  if ($xpathQueryResultCount == 1) {
    return $target;
  }
  else {
    return '';
  }
}

/**
 *
 * Get the content model associated with the PID of the
 * object passed in as a parameter. If more than one
 * content model (that is not part of the core processing)
 * is associated with the object, output an error message
 * and return an empty string.
 *
 * @param string $pid
 *   The PID of the object for which the content
 *   model is to be found.
 * return string
 *   The PID of the content model or an empty string.
 */
function get_object_linker_content_model($pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $spoSubject = '<info:fedora/' . $pid . '>';
  $spoPredicate = '<info:fedora/fedora-system:def/model#hasModel>';
  $spoObject = '*';
  $contentModels = spo_search($spoSubject, $spoPredicate, $spoObject);
  $contentModelsTot = count($contentModels);

  foreach ($contentModels as $contentModel) {
    switch ($contentModel['objectPid']) {
      case "fedora-system:FedoraObject-3.0":
      case "si:resourceCModel":
      case "si:conceptCModel":
        $contentModelsTot -= 1;
        break;
      default:
        $objectLinkerContentModel = $contentModel['objectPid'];
    }
  }
  if ($contentModelsTot == 1) {
    return $objectLinkerContentModel;
  }
  else {
    $msg = $pid . ', has ' . $contentModelsTot . ' content models attached so the form name could not be determined and so any links created by the Object Linker cannot be processed.';
    drupal_set_message(t($msg), 'error');
    return '';
  }
}

/**
 *
 * Get the form name associated with the object PID and
 * content model ID that are passed as parameters.
 * If more than one form is associated with the content
 * model, output an error message and return an empty
 * string.
 *
 * @param string $objectPid
 *   The PID of the object for which the form name associated with
 *   the content model is to be found.
 * @param string $contentModelPid
 *   The PID of the content model associated with form name.
 * return string
 *   The XML-form name or an empty string.
 */
function get_object_linker_form_name($objectPid, $contentModelPid) {
  module_load_include('inc', 'fedora_repository', 'ConnectionHelper');
  module_load_include('inc', 'xml_form_builder', 'XMLFormDatabase');

  $formNameSelect = "SELECT form_name FROM {islandora_content_model_forms} WHERE content_model = '" . $contentModelPid . "'";
  $formNameSelectResult = db_query($formNameSelect);
  $formNameTot = 0;
  while ($data = db_fetch_object($formNameSelectResult)) {
    $objectLinkerFormName = $data->form_name;
    $formNameTot += 1;
  }

  if ($formNameTot == 0) {
    $objectLinkerFormName = '';
    $msg = $objectPid . ' has no form associated with the content model ' . $contentModelPid . ', therefore any links that were created by the Object Linker could not be processed.';
    drupal_set_message(t($msg), 'error');
  }
  else if ($formNameTot > 1) {
    $objectLinkerFormName = '';
    $msg = $objectPid . ', has more than one form associated with the content model ' . $contentModelPid . ', therefore any links that were created by the Object Linker could not be processed.';
    drupal_set_message(t($msg), 'error');
  }

  return $objectLinkerFormName;
}

/**
 *
 * Build the XML-form with the name passed as a parameter.
 * This ensures that the 'rdf_relationships' variable
 * is populated with the configuration info present in the
 * RDF-autocomplete entries associated with the XML-form.
 *
 * @param string $formName
 *   The XML-form name to be built.
 *  
 * return
 *   None
 */
function build_object_linker_form($formName) {
  module_load_include('inc', 'xml_form_builder', 'XMLFormDatabase');
  module_load_include('inc', 'xml_form_api', 'XMLForm');
  module_load_include('inc', 'xml_form_api', 'XMLFormDefinition');

  $formState = array();

  try {
    $objectLinkerXmlForm = new XMLForm($formState);
    if (!$objectLinkerXmlForm->isInitialized()) {
      // Load up the form in order to populate the
      // rdf_relationships variable for the form.
      $object_linker_definition = new XMLFormDefinition(XMLFormDatabase::Get($formName));
      $object_linker_form = $object_linker_definition->getForm();

      unset($objectLinkerXmlForm);
      unset($object_linker_definition);
      unset($object_linker_form);
      unset($formState);
    }
  }
  catch (Exception $e) {
    $msg = "File: {$e->getFile()}<br/>Line: {$e->getLine()}<br/>Error: {$e->getMessage()}";
    drupal_set_message(filter_xss(t($msg)), 'error');
  }
}

/**
 *
 * Register the namespace info passed in as a parameter
 * in the domxpath that is also passed as a parameter
 * so that the xml elements wth those namespaces can
 * be searched.
 *
 * @param array $domxpath
 *   The domxpath in which the namespaces are to be registered
 * @param string $namespaceInfo
 *   One or more namespace-prefix/url pairs that are to be
 *   registered.
 * @param boolean $checkPrevious
 *   If this is set the array $namespacedPrefixesRegistered
 *   is checked to see if the namespace has already been
 *   registered.
 * @param array $namespacePrefixesRegistered
 *   If $checkPrevious is set to true this array is checked
 *   for a match prior to registering the namepace. If there
 *   is a match it is not re-registered. If there is no
 *   match, it is registered and the namespace prefix is
 *   added to the array.
 * return
 *   None
 */
function register_xpath_namespaces(&$domxpath, &$namespaceInfo, $checkPrevious = false, &$namespacePrefixesRegistered = NULL) {
  $namespaceDelimiter = '==';
  $namespaceComponentDelimiter = '!!';
  $namespaces = explode($namespaceDelimiter, $namespaceInfo);
  $namespaceCount = 0;
  foreach ($namespaces as $namespace) {
    $namespaceComponents = explode($namespaceComponentDelimiter, $namespace);
    $componentCount = count($namespaceComponents);
    if ($componentCount == 2) {
      if ($checkPrevious) {
        $alreadyRegistered = false;
        if (count($namespacePrefixesRegistered) > 0) {
          foreach ($namespacePrefixesRegistered as $namespacePrefixRegistered) {
            if (!strcmp($namespacePrefixRegistered, $namespaceComponents[0])) {
              $alreadyRegistered = true;
              break;
            }
          }
        }
        if (!$alreadyRegistered) {
          $domxpath->registerNamespace($namespaceComponents[0], $namespaceComponents[1]);
          $namespacePrefixesRegistered[] = $namespaceComponents[0];
        }
      }
      else {
        $domxpath->registerNamespace($namespaceComponents[0], $namespaceComponents[1]);
      }
    }
    else {
      drupal_set_message(t('The name space information has been incorrectly delimited for object linking.'));
    }
    $namespaceCount += 1;
  }

  if ($namespaceCount == 0) {
    drupal_set_message(t('The required name space information has not been entered for object linking.'));
  }
}

/**
 *
 * Display in HTML the header followed by the rows
 * returned from the Sparql query as a list of
 * links.
 *
 * @param string $header
 *   The header of the list of links.
 * @param string $sparqlQuery
 *   The sparql query used to populate the list
 * @param string $clickable
 *   Determines whether the values are clickable
 * return
 *   Formatted list or empty string
 */
function get_link_list($header, $sparqlQuery, $clickable = true) {
  global $base_url;
  module_load_include('module', 'subpath_alias', 'subpath_alias');

  $sparql_field = <<<EOQ
$sparqlQuery
EOQ;
  $queryResults = get_link_array($sparqlQuery);
  if ($queryResults) {
    $formattedHtml = '<div style="clear:both"><p>' . $header;
    $formattedHtml .= '<ul style="list-style:none">';
    foreach ($queryResults as $result) {
      if ($clickable) {
        //SFOX New code to apply the pathauto translation(s)
        $path = 'fedora/repository/' . $result['pid'];
        $original_path = 'fedora/repository/' . $result['pid'];
        $options['language'] = 'en';
        subpath_alias_url_outbound_alter($path, $options, $original_path);
        $formattedHtml .= '<li>' . '<a href="' . $base_url . '/' . $path . '">' . $result['title'] . '</a></li>';
      }
      else {
        $formattedHtml .= '<li>&nbsp;' . $result['title'] . '</li>';
      }
    }
    $formattedHtml .= '</ul></p></div>';
    return $formattedHtml;
  }
  else {
    return '';
  }
}

/**
 *
 * Perform sparql query and return resulting array.
 *
 * @param string $sparqlQuery
 *   The sparql query
 * @return array
 *   Query results or NULL
 *   
 */
function get_link_array($sparqlQuery) {
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  global $base_url;
  $sparql_field = <<<EOQ
$sparqlQuery
EOQ;
  $limit = -1;
  $offset = 0;
  $queryResults = ObjectHelper::performRiQuery($sparqlQuery, 'sparql', $limit, $offset);
  if (count($queryResults) > 0) {
    return $queryResults;
  }
  else {
    return NULL;
  }
}

/**
 *
 * Perform an spo search based on the subject and
 * predicate. If the spo search returns any rows,
 * a sparql query is constructed and executed that
 * returns the titles and PIDs of the fedora-objects
 * corresponding to the object of the RDF.
 * It is probably possible to achieve the same result
 * and better performance by constructing a single
 * sparql query, but I do not have time to figure
 * out how to construct it at this moment.
 *
 * @param string $subjectPid
 *   The subject PID for the spo search
 * @param string $predicate
 *   The predicate for the spo search
 * @param integer $limit
 *   A limit for the sparql query
 * @param integer $returnType
 *   Determines whether an array or html-bounded list is returned
 * @param string $header
 *   If html-bounded list type is stipulated then this is the header
 *   for the list.
 *  
 * return array
 *   Query results
 *   
 */
function get_rdf_object_entries($subjectPid, $predicate, $limit, $returnType, $header) {
  $arrayType = 0;
  $clickableListType = 1;
  $nonClickableListType = 2;
  $noLimit = -1;
  $object = '*';
  $subject = '<info:fedora/' . $subjectPid . '>';
  $spoResults = spo_search($subject, $predicate, $object);
  $spoResultsTot = count($spoResults);

  if ($spoResultsTot > 0) {
    $spoResultsCount = 0;
    $filterComponent = 'FILTER ((';
    foreach ($spoResults as $spoResult) {
      $spoResultsCount += 1;
      if ($spoResultsCount < $spoResultsTot) {
        $filterComponent .= 'sameTerm(?subject, <info:fedora/' . $spoResult['objectPid'] . '>)' . ' || ';
      }
      else {
        $filterComponent .= 'sameTerm(?subject, <info:fedora/' . $spoResult['objectPid'] . '>)' . '))';
      }
    }
    $sparqlQuery = 'SELECT ?title ?pid WHERE { ?subject <' . PURL_ELEMENTS_URI . 'identifier> ?pid; <' . PURL_ELEMENTS_URI . 'title> ?title ' . $filterComponent . ' } ORDER BY ?title';

    if ($limit > $noLimit) {
      $sparqlQuery = $sparqlQuery . ' LIMIT ' . $limit;
    }
    if ($returnType == $arrayType) {
      return get_link_array($sparqlQuery);
    }
    else if ($returnType == $clickableListType) {
      return get_link_list($header, $sparqlQuery, true);
    }
    else {
      return get_link_list($header, $sparqlQuery, false);
    }
  }
  else {
    return NULL;
  }
}

/**
 *
 * Construct a sparql query that returns the titles
 * and PIDs of the fedora-objects corresponding to
 * the object of the RDF.
 *
 * @param string $predicate
 *   The predicate for the sparql query
 * @param string $objectPid
 *   The object PID for the sparql query
 * @param integer $limit
 *   A limit for the sparql query
 * @param integer $returnType
 *   Determines whether an array or html-bounded list is returned
 * @param string $header
 *   If html-bounded list type is stipulated then this is the header
 *   for the list.
 * return array
 *   Query results
 *   
 */
function get_rdf_subject_entries($objectPid, $predicate, $limit, $returnType, $header) {
  $arrayType = 0;
  $clickableListType = 1;
  $nonClickableListType = 2;
  $noLimit = -1;
  $object = '<info:fedora/' . $objectPid . '>';
  $filterComponent = 'FILTER (sameTerm(?object,' . $object . '))';
  $sparqlQuery = 'SELECT ?title ?pid WHERE { ?subject ' . $predicate . ' ?object;  <' . PURL_ELEMENTS_URI . 'title> ?title; <' . PURL_ELEMENTS_URI . 'identifier> ?pid ' . $filterComponent . ' } ORDER BY ?title';
  if ($limit > $noLimit) {
    $sparqlQuery = $sparqlQuery . ' LIMIT ' . $limit;
  }
  if ($returnType == $arrayType) {
    return get_link_array($sparqlQuery);
  }
  else if ($returnType == $clickableListType) {
    return get_link_list($header, $sparqlQuery, true);
  }
  else {
    return get_link_list($header, $sparqlQuery, false);
  }
}

/**
 *
 * Check whether an object with a title equivalent to
 * the value passed in as a label already exists according
 * to the criteria contained in the sparql query supplied.
 *
 * @param string $label
 *   The label field from the object
 * @param string $sparqlQuery
 *   The sparql query to be used to check for a duplicate. It
 *   should select only 'title'.
 * return string
 *   Either NULL when no duplicate is found, or an error message
 *   when a duplicate is found.
 *   
 */
function check_for_duplicate_label($label, $sparqlQuery) {
  $linkArray = get_link_array($sparqlQuery);
  if (count($linkArray) > 0) {
    foreach ($linkArray as $linkItem) {
      $existingTitle = $linkItem['title'];
      break;
    }
    if (strcmp($label, $existingTitle)) {
      return 'The entry "' . $label . '" is already present within this collection as "' . $existingTitle . '", so it has not been created.';
    }
    else {
      return 'The entry "' . $label . '" is already present within this collection, so it has not been created.';
    }
  }
  else {
    return NULL;
  }
}

/**
 *
 * Add a new object with the appropriate XML and RDF links
 * as a batch operation.
 * Note that $rdf_relationship and $link are not passed by
 * reference in this instance because the original arrays
 * might no longer exist by the time this batch operation
 * is called.
 *
 * @param string $newObjectPid
 *   The Persistent Identifier for the new object
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $subjectPid
 *   The Persistent Identifier for the subject object
 * @param string $data
 *   The character string entered by the user
 * @param boolean $editFlag
 *   Indicates whether the subject object is being edited.
 * @param boolean $postEditSubjectTitle
 *   Contains the title of the subject object after
 *   the XML form has been edited by the user.
 * @param string $subjectDCTitle
 *   Contains the title of the subject object prior 
 *   to the XML form being edited by the user.
 * @param string $data
 *   Contains the content of the relevant XML element
 *   of the XML form after it has been edited.
 * @param string $context
 *   Required for batch processing
 * return
 *   None
 */

function add_object_batch($newObjectPid, $rdf_relationship, $subjectPid, $data, $editFlag, $postEditSubjectTitle, $subjectDCTitle, $data, &$context) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'islandora_content_model_forms', 'FOXML');

  $newObjectDoc = new domDocument();

  $newObjectDatastreamXml = $rdf_relationship['object_datastream_xml'];
  // Add the title of the object to the new object datastream.
  if ($rdf_relationship['object_xpath_title_search']) {
    // The XML has been supplied in the sparql autocomplete form
    // by the user so should contain the relevant XML title
    // element. The "build" and "countback" arguments are therefore
    // redundant because they are only needed when the XML
    // field is not found and it has to be created.
    if (!(add_to_datastream($rdf_relationship, $newObjectDatastreamXml, $rdf_relationship['object_xpath_title_search'], 'redundant', 'redundant', $data))) {
      $context['results']['errorFlag'] = 1;
      $context['results']['currentPid'] = $newObjectPid;
    }
  }
  if ($rdf_relationship['object_xpath_date_create_search']) {
    // Add a solr-friendly-creation-date to the new object
    // datastream.
    $solrDate = sprintf("%sT%s.000Z", date("Y-m-d"), date("H:i:s"));
    // The XML has been supplied in the sparql autocomplete form
    // by the user so should contain the relevant XML creation
    // date element. The "build" and "countback" arguments are
    // therefore redundant.
    if (!(add_to_datastream($rdf_relationship, $newObjectDatastreamXml, $rdf_relationship['object_xpath_date_create_search'], 'redundant', 'redundant', $solrDate))) {
      $context['results']['errorFlag'] = 1;
      $context['results']['currentPid'] = $newObjectPid;
    }
  }
  $newObjectDoc->loadXML($newObjectDatastreamXml);
  $foxml = new FOXML($data, $newObjectPid, $rdf_relationship['object_dsid'], $rdf_relationship['object_content_model_pid'], $rdf_relationship['object_collection_pid'], 'isMemberOfCollection', null, $newObjectDoc, $rdf_relationship['object_transform'], $rdf_relationship['object_state']);
  $foxml->ingest();
}

/**
 *
 * Remove the inverse RDF from object to subject and remove the
 * subject title from the object datastream when appropriate.
 * Note that $rdf_relationship is not passed by reference in
 * this instance because the original arrays might no longer
 * exist by the time this batch operation is called.
 *
 * @param string $subjectPid
 *   The Persistent Identifier of the subject component the RDF
 * @param string $objectPid
 *   The Persistent Identifier of the object component the RDF
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $rdfNamespace
 *   The namespace of the predicate component in the inverse RDF
 * @param string $inverseName
 *   The verb of the predicate component in the inverse RDF
 * @param int $linkTypeOutwardAndInverse
 *   Constant for link type that is subject to object and object to subject
 * @param string $context
 *   Required for batch processing
 * return
 *   None
 */

function remove_inverse_rdf_batch($subjectPid, $objectPid, $rdf_relationship, $rdfNamespace, $inverseName, $subjectDCTitle, &$context) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');
  $object = islandora_object_load($objectPid);
  if ($object) {
    $object->relationships->remove($rdfNamespace, $inverseName, $subjectPid);
    // Check if inverse-object-symmetric datastream processing
    // is required.
    if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
      remove_from_datastream($rdf_relationship, $objectPid, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build_countback'], $subjectDCTitle);
    }
  }
  else {
    $context['results']['errorFlag'] = 1;
    $context['results']['currentPid'] = $subjectPid;
  }
}

/**
 *
 * Replace the data in an xml element of a datastream as a
 * batch operation.
 * Note that $rdf_relationship and $link are not passed by
 * reference in this instance because the original arrays
 * might no longer exist by the time this batch operation
 * is called.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $objectPid
 *   The Persistent Identifier of the object in which the datastream
 *   is to be updated
 * @param string $targetData
 *   Contains the target data in the datastream
 * @param string $replacementData
 *   Contains the replacement data for the datastream
 * @param string $context
 *   Required for batch processing
 * return
 *   None
 */

function replace_text_in_datastream_batch($rdf_relationship, $objectPid, $targetData, $replacementData, &$context) {
  replace_text_in_datastream($rdf_relationship, $objectPid, $rdf_relationship['object_xpath_data_search'], $targetData, $replacementData);
}

/**
 *
 * Create RDF links between subject-objects and object-objects 
 * as a batch operation.
 * Note that $rdf_relationship and $link are not passed by
 * reference in this instance because the original arrays
 * might no longer exist by the time this batch operation
 * is called.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param string $subjectPid
 *   The Persistent Identifier of the subject-object in the RDF
 * @param string $objectPid
 *   The Persistent Identifier of the object-object in the RDF
 * @param string $rdfName
 *   The verb of the predicate component in the RDF
 * @param string $rdfNamespace
 *   The stem of the predicate component in the RDF
 * @param string $inverseName
 *   The verb of the predicate component in the inverse RDF
 * @param string $inversePredicate
 *   The inverse of the predicate associated with the subject-object
 * @param boolean $editFlag
 *   Indicates whether the subject-object is being edited.
 * @param string $subjectDCTitle
 *   Contains the title of the subject object prior 
 *   to the XML form being edited by the user.
 * @param string $postEditSubjectTitle
 *   Contains the title of the subject object after
 *   the XML form has been edited by the user.
 * @param int $linkTypeOutwardOnly
 *   Constant for link type that is subject to object only
 * @param int $linkTypeOutwardAndInverse
 *   Constant for link type that is subject to object and object to subject
 * @param int $linkTypeInverseOnly
 *   Constant for link type that is object to subject only
 * @param string $context
 *   Required for batch processing
 * return
 *   None
 */

function create_rdf_links_batch($rdf_relationship, $subjectPid, $objectPid, $rdfName, $rdfNamespace, $inverseName, $inversePredicate, $editFlag, $subjectDCTitle, $postEditSubjectTitle, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly, &$context) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');

  $subject = islandora_object_load($subjectPid);
  if ($subject) {
    // Even when creating a new subject-object, the user could have
    // added identical entries in multiple fields, so check whether
    // a link to this object already exists.

    if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
      $spoSubject = '<info:fedora/' . $subjectPid . '>';
      $spoPredicate = '<' . $rdf_relationship['rdf_predicate'] . '>';
      $spoObject = '<info:fedora/' . $objectPid . '>';
    }
    else {
      // It must be an inverse-only type of link.
      $spoSubject = '<info:fedora/' . $objectPid . '>';
      $spoPredicate = '<' . $inversePredicate . '>';
      $spoObject = '<info:fedora/' . $subjectPid . '>';
    }
    $existingObjectTriple = spo_search($spoSubject, $spoPredicate, $spoObject);
    if (count($existingObjectTriple) == 0) {
      // Add a subject-to-object RDF.
      if ($rdf_relationship['link_type'] != $linkTypeInverseOnly) {
        $subject->relationships->add($rdfNamespace, $rdfName, $objectPid);
      }
      if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse || $rdf_relationship['link_type'] == $linkTypeInverseOnly) {
        $object = islandora_object_load($objectPid);
        if ($object) {
          // Add an object-to-subject RDF.
          $object->relationships->add($rdfNamespace, $inverseName, $subjectPid);
          // Check if symmetric object datastream processing
          // is required.
          if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
            $objectDatastreamID = $rdf_relationship['object_dsid'];
            $objectDatastream = $object[$objectDatastreamID];
            $objectDatastreamXml = $objectDatastream->content;
            if (!(add_to_datastream($rdf_relationship, $objectDatastreamXml, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build'], $rdf_relationship['object_xpath_data_build_countback'], $editFlag ? $postEditSubjectTitle : $subjectDCTitle))) {
              $context['results']['currentPid'] = $subjectPid;
              $context['results']['errorFlag'] = 1;
            }
            // Update the datastream with the xml containing the added node.
            $objectDatastream->content = $objectDatastreamXml;
          }
        } else {
          $context['results']['currentPid'] = $subjectPid;
          $context['results']['errorFlag'] = 1;
        }
      }
    }
  } else {
    $context['results']['currentPid'] = $subjectPid;
    $context['results']['errorFlag'] = 1;
  }
}

/**
 *
 * The batch processing of RDF links and datastream updates
 * involving subject-objects and object-objects that is
 * required as a result of an edit operation being performed.
 * Note that $rdf_relationship and $link are not passed by
 * reference in this instance because the original arrays
 * might no longer exist by the time this batch operation
 * is called.
 *
 * @param array $rdf_relationship
 *   Corresponds to a single RDF-autocomplete associated with an XML-form.
 * @param int $rdfRelationshipRdfLinkTot
 *   The number of links associated with the relationship
 * @param string $subjectPid
 *   The Persistent Identifier of the subject-object in the RDF
 * @param string $objectPid
 *   The Persistent Identifier of the object-object in the RDF
 * @param string $rdfName
 *   The verb of the predicate component in the RDF
 * @param string $rdfNamespace
 *   The stem of the predicate component in the RDF
 * @param string $inverseName
 *   The verb of the predicate component in the inverse RDF
 * @param string $inversePredicate
 *   The inverse of the predicate associated with the subject-object
 * @param boolean $editFlag
 *   Indicates whether the subject-object is being edited.
 * @param string $subjectDCTitle
 *   Contains the title of the subject object prior 
 *   to the XML form being edited by the user.
 * @param string $postEditSubjectTitle
 *   Contains the title of the subject object after
 *   the XML form has been edited by the user.
 * @param int $linkTypeOutwardOnly
 *   Constant for link type that is subject to object only
 * @param int $linkTypeOutwardAndInverse
 *   Constant for link type that is subject to object and object to subject
 * @param int $linkTypeInverseOnly
 *   Constant for link type that is object to subject only
 * @param string $context
 *   Required for batch processing
 * return 
 *   None
 */

function process_edit_batch($rdf_relationship, $rdfRelationshipRdfLinkTot, $subjectPid, $rdfName, $rdfNamespace, $inverseName, $inversePredicate, $preEditObjectTripleForRelationship, $replaceSubjectTitleInObjectDatastream, $preEditSubjectTitle, $postEditSubjectTitle, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly, &$context) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');

  $subject = islandora_object_load($subjectPid);
  if ($subject) {
    if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
      $preEditPid = $preEditObjectTripleForRelationship['objectPid'];
    }
    else {
      $preEditPid = $preEditObjectTripleForRelationship['subjectPid'];
    }
    if (strlen($rdf_relationship['subject_xpath_data_search']) > 0) {
      // Check whether entries relating to this link have been
      // removed during the edit.
      $preEditMatchFound = false;
      if ($rdfRelationshipRdfLinkTot > 0) {
        foreach ($rdf_relationship['rdf_link'] as $postEditLink) {
          if (!strcmp($preEditPid, $postEditLink['pid'])) {
            $preEditMatchFound = true;
            break;
          }
        }
      }
      if (!$preEditMatchFound) {
        // The current $preEditPid for the object is not present
        // in the array of links that are required post-edit, so
        // unwind any links and datastream entries that are
        // associated with the object.
        $preEditObject = islandora_object_load($preEditPid);
        if ($preEditObject) {
          if ($rdf_relationship['link_type'] != $linkTypeInverseOnly) {
            $subject->relationships->remove($rdfNamespace, $rdfName, $preEditObject);
          }
          if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse || $rdf_relationship['link_type'] == $linkTypeInverseOnly) {
            $preEditObject->relationships->remove($rdfNamespace, $inverseName, $subject);
          }
          // Check if inverse-object-symmetric link processing
          // is involved.
          if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
            remove_from_datastream($rdf_relationship, $preEditPid, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build_countback'], $preEditSubjectTitle);
          }
        }
        else {
          $context['results']['errorFlag'] = 1;
          $context['results']['currentPid'] = $subjectPid;
        }
      }
    }
    else {
      if ($replaceSubjectTitleInObjectDatastream && $rdf_relationship['symmetry_in_object_datastream_required']) {
        // There is no xml field for the object-title associated with
        // this link for the subject-object so there is no checking
        // to be done regarding pre-edit versus post-edit existence.
        // Therefore the subject-title should be updated where it 
        // appears in the datastream of the linked object. An example
        // of such a scenario would be when the title of a
        // keyword-subject-object is modified and the catalogue records
        // that point to it to have their keyword entries updated.
        replace_text_in_datastream($rdf_relationship, $preEditPid, $rdf_relationship['object_xpath_data_search'], $preEditSubjectTitle, $postEditSubjectTitle);
      }
    }
  } else {
    $context['results']['errorFlag'] = 1;
    $context['results']['currentPid'] = $subjectPid;
  }
}

/**
  * The final function called by the drupal batch processing
  * functionality.
  * @return
  *   None
  */
function completeObjectLinkerBatchProcessing($success, $results, $operations) {
  if ($results['errorFlag'] == 1) {
    $msg = 'A linking error occurred while processing the object with the ID ' . $results['currentPid'];
    drupal_set_message(t($msg));
  }
  drupal_set_message($results['msg']);
  $context['finished'] = 1;
}
