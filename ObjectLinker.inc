<?php
/**
 * @file
 * @author Nicholas Bywell of the Freshwater Biological Association (FBA).
 *
 * This file contains the bulk of the functions that handle Object Linking
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
 *
 * @return
 *   Modifications to the parameter passed by reference.
 *   Setting the content of the 'rdf_relationships' variable.
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
      register_xpath_namespaces($domxpath,$rdf_relationship['subject_namespace_info'], $checkPreviousNamespacePrefixes, $namespacePrefixesRegistered);
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
                  $dataMinusPid = substr($objectItem,0,strlen($objectItem)-(strlen($objectPid) + (2*$charsInDelim) + 1));
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
 * which relates to one or more RDF-autocomplete entries associated
 * with the XML-Form.
 * RDF-links and inverse-links are created between subject and object
 * where appropriate. The relevant datastreams in the objects are updated
 * with symmetrical entries, where appropriate.
 *
 * @param array $form
     The data relating to the XML-form that has been submitted.
 * @param array $form_state
 * @param int $editFlag
 *   Indicates whether processing the ingest of an object (FALSE)
 *   or the edit of an existing object (TRUE)
 *
 * @return
 *   The 'rdf_relationships' variable is re-initialized back to NULL.
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
      $subjectDCTitle = get_dc_title($subject);
      $rdfRelationshipCount = 0;

      // For Fidora processing, construct the user collection.
      // This is irrelevant for non-Fidora processing.
      $userId = $user->islandora_user_pid;
      if (strlen($userId) > 0) {
        $userCollectionNs = str_replace(array('-',':'),'', $userId);
        $userCollection = $userCollectionNs . ':collection';
      }

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
          } else {
            $inverseError = true;
          }
          if ($inverseError) {
            if ($editFlag) {
              $msgInsert = $postEditSubjectTitle;
            } else {
              $msgInsert = $subjectDCTitle;
            }
            $msg = 'While creating or editing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create an inverse of the "' . $rdfName . '" link, the inverse was not found.' ; 
            drupal_set_message(t($msg));
            break;
          }
        }
        if ($editFlag) {
          if ($rdfRelationshipCount == 1) {
            // Get the pre and post edit title values of
            // the subject-object.
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
              } else {
                $msg = 'While processing "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" the path corresponding to the "Subject XPath Title Search" field in the autocomplete record for the "' . $rdfName . '" link was not found.'; 
                drupal_set_message(t($msg));
              }
            } else {
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
          } else {
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
          process_instances_of_this_relationship($rdf_relationship, $editFlag, $userCollection, $preEditObjectTriplesForRelationship, $replaceSubjectTitleInObjectDatastream, $subjectPid, $subject, $subjectDCTitle, $preEditSubjectTitle, $postEditSubjectTitle, $rdfName, $inverseName, $rdfNamespace, $linkTypeOutwardOnly, $linkTypeOutwardAndInverse, $linkTypeInverseOnly);
        }
        if ($editFlag) {
          if ($preEditObjectTriplesForRelationship) {
            foreach ($preEditObjectTriplesForRelationship as $preEditObjectTripleForRelationship) {
              if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
                $preEditPid = $preEditObjectTripleForRelationship['objectPid'];
              } else {
                $preEditPid = $preEditObjectTripleForRelationship['subjectPid'];
              }
              if (strlen($rdf_relationship['subject_xpath_data_search']) > 0) {
                // Check whether entries relating to this link have been
                // removed during the edit.
                $preEditMatchFound = false;
                if ($rdfRelationshipRdfLinkTot > 0) {
                  foreach ($rdf_relationship['rdf_link'] as $postEditLink) {
                    if (!strcmp($preEditPid,$postEditLink['pid'])) {
                      $preEditMatchFound = true;
                      break;
                    }
                  }
                }
                if (!$preEditMatchFound) {
                  $preEditObject = islandora_object_load($preEditPid);
                  if ($preEditObject) {
                    if ($rdf_relationship['link_type'] != $linkTypeInverseOnly) {
                      $subject->relationships->remove($rdfNamespace, $rdfName, $preEditObject);
                    }
                    if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse || $rdf_relationship['link_type'] == $linkTypeInverseOnly) {
                      $preEditObject->relationships->remove($rdfNamespace, $inverseName, $subject);
                    }
                    // Check if inverse-object-symmetric link processing
                    // is required.
                    if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
                      remove_from_datastream($rdf_relationship, $preEditPid, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build_countback'], $preEditSubjectTitle);
                    }
                  } else {
                    if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
                      $msg = 'While processing the linking for "' . $preEditSubjectTitle . '" with ID "' . $subjectPid . '" and attempting to remove both a "' . $rdfName . '" link to and a "' . $inverseName . '" inverse link from "' . $preEditPid . '" the operation failed because the object "' . $preEditPid . '" was not found.' ;
                      drupal_set_message(t($msg));
                    } elseif ($rdf_relationship['link_type'] == $linkTypeInverseOnly) {
                      $msg = 'While processing the linking for "' . $preEditSubjectTitle . '" with ID "' . $subjectPid . '" and attempting to remove a "' . $inverseName . '" inverse link from "' . $preEditPid . '" the operation failed because the object "' . $preEditPid . '" was not found.' ;
                    } else {
                      $msg = 'While processing the linking for "' . $preEditSubjectTitle . '" with ID "' . $subjectPid . '" and attempting to remove a "' . $rdfName . '" link to "' . $preEditPid . '" the operation failed because the object "' . $preEditPid . '" was not found.' ;
                      drupal_set_message(t($msg));
                    }
                  }
                }
              } else {
                if ($replaceSubjectTitleInObjectDatastream && $rdf_relationship['symmetry_in_object_datastream_required']) {
                  // There is no subject xml field associated with this
                  // link for this subject-object so the titles should
                  // be replaced in ALL the linked objects.
                  replace_text_in_datastream($rdf_relationship, $preEditPid, $rdf_relationship['object_xpath_data_search'], $preEditSubjectTitle, $postEditSubjectTitle);
                }
              }
            }
          }
        }
      }
    } else {
      $msg = 'While processing the linking for a record with ID "' . $subjectPid . '" the subject-object failed to load.';
      drupal_set_message(t($msg));
    }   
  }
}

/**
 * Processes RDF-links and datastream modifications corresponding
 * to specific entries in the subject datastream.
 *
 * @param array   $rdf_relationship
 * @param integer $editFlag
 * @param string  $userCollection,
 * @param array   $preEditObjectTriplesForRelationship,
 * @param integer $replaceSubjectTitleInObjectDatastream,
 * @param string  $subjectPid,
 * @param array   $subject,
 * @param string  $subjectDCTitle,
 * @param string  $preEditSubjectTitle,
 * @param string  $postEditSubjectTitle,
 * @param string  $rdfName,
 * @param string  $inverseName,
 * @param string  $rdfNamespace,
 * @param integer $linkTypeOutwardOnly,
 * @param integer $linkTypeOutwardAndInverse,
 * @param integer $linkTypeInverseOnly
 *
 * @return
 */
function process_instances_of_this_relationship(
  &$rdf_relationship,
  &$editFlag,
  &$userCollection,
  &$preEditObjectTriplesForRelationship,
  &$replaceSubjectTitleInObjectDatastream,
  &$subjectPid,
  &$subject,
  &$subjectDCTitle,
  &$preEditSubjectTitle,
  &$postEditSubjectTitle,
  &$rdfName,
  &$inverseName,
  &$rdfNamespace,
  &$linkTypeOutwardOnly,
  &$linkTypeOutwardAndInverse,
  &$linkTypeInverseOnly) {

  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'islandora_autocomplete_sparql', 'includes/islandora_autocomplete_sparql.vocab');
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
        $sparqlTestObjectExistence = sprintf('SELECT ?title ?object WHERE { ?object <http://purl.org/dc/elements/1.1/title> ?title; <fedora-model:hasModel> <info:fedora/%s>; <fedora-rels-ext:isMemberOfCollection> <info:fedora/%s> FILTER (regex(str(?title), "^%s$", "i"))}', $rdf_relationship['object_content_model_pid'], $rdf_relationship['object_collection_pid'], escape_for_sparql($link['data']));
      } else {
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

      // For Fidora processing, replace the user-collection-place-holder
      // with the actual user collection.
      if (strlen($userCollection) > 0 && strpos($sparqlTestObjectExistence, $userCollectionPlaceHolder)) {
        $userCollectionFullPid = 'info:fedora/' . $userCollection;
        $sparqlTestObjectExistence = str_replace($userCollectionPlaceHolder, $userCollectionFullPid, $sparqlTestObjectExistence);
      }
      $limit = -1; $offset = 0;
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
          $newObjectDoc = new domDocument();

          $newObjectDatastreamXml = $rdf_relationship['object_datastream_xml'];
          // Add the title of the object to the new object datastream.
          if ($rdf_relationship['object_xpath_title_search']) {
            // The XML has been supplied in the sparql autocomplete form
            // by the user so should contain the relevant XML title
            // element. The "build" and "countback" arguments are
            // therefore redundant.
            add_to_datastream($subjectPid, $editFlag ? $postEditSubjectTitle : $subjectDCTitle, $rdf_relationship, $newObjectDatastreamXml, $rdf_relationship['object_xpath_title_search'], 'redundant', 'redundant', $link['data']);
          }
          if ($rdf_relationship['object_xpath_date_create_search']) {
            // Add a solr-friendly-creation-date to the new object
            // datastream.
            $solrDate = sprintf("%sT%s.000Z", date("Y-m-d"),date("H:i:s"));
            // The XML has been supplied in the sparql autocomplete form
            // by the user so should contain the relevant XML creation
            // date element. The "build" and "countback" arguments are
            // therefore redundant.
            add_to_datastream($subjectPid, $editFlag ? $postEditSubjectTitle : $subjectDCTitle, $rdf_relationship, $newObjectDatastreamXml, $rdf_relationship['object_xpath_date_create_search'], 'redundant', 'redundant', $solrDate);
          }
          $newObjectDoc->loadXML($newObjectDatastreamXml);
          $foxml = new FOXML($link['data'], $newObjectPid, $rdf_relationship['object_dsid'], $rdf_relationship['object_content_model_pid'], $rdf_relationship['object_collection_pid'], 'isMemberOfCollection', null, $newObjectDoc, $rdf_relationship['object_transform'], $rdf_relationship['object_state']);
          $foxml->ingest();
          $link['pid'] = $newObjectPid;
          $rdf_relationship['rdf_link'][$rdfRelationshipRdfLinkCount][pid] = $newObjectPid;
        }
      } else if ($objectExistenceCount == 1) {
        $link['pid'] = $objectExistencePid;
        $rdf_relationship['rdf_link'][$rdfRelationshipRdfLinkCount][pid] = $objectExistencePid;

        if ($editFlag && $rdf_relationship['symmetry_in_object_datastream_required'] && $replaceSubjectTitleInObjectDatastream) {
          // The editor has changed the title of the subject-object
          // so this must be reflected in all the object-object
          // datastreams that contain it.
          if ($preEditObjectTriplesForRelationship) {
            $preEditMatchFound = false;
            foreach ($preEditObjectTriplesForRelationship as $preEditObjectTripleForRelationship) {
              if (!strcmp($preEditObjectTripleForRelationship['objectPid'], $objectExistencePid)) {
                $preEditMatchFound = true;
              }
            }
            if ($preEditMatchFound) {
              replace_text_in_datastream($rdf_relationship, $objectExistencePid, $rdf_relationship['object_xpath_data_search'], $preEditSubjectTitle, $postEditSubjectTitle);
            }
          }
        }
      } else {
        if ($editFlag) {
          $msgInsert = $postEditSubjectTitle;
        } else {
          $msgInsert = $subjectDCTitle;
        }
        $msg = 'While processing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create a "' . $rdfName . '" link to "' . $link['data'] . '" more than one record was found with the same title, so the link was not created.' ;
        //drupal_set_message(t($msg));
      }
    }
    // If an object-pid exists create the appropriate RDF links.
    if (strcmp($link['pid'], 'nopid')) {
      $objectPid = $link['pid'];
      // Check whether the object exists.
      $object = islandora_object_load($objectPid);
      if ($object) {
        // Even when creating a new subject-object, the user could have
        // added identical entries in multiple fields, so check whether
        // a link to this object already exists.

        if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
          $spoSubject = '<info:fedora/' . $subjectPid . '>';
          $spoPredicate = '<' . $rdf_relationship['rdf_predicate'] . '>';
          $spoObject = '<info:fedora/' . $objectPid . '>';
        } else {
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
            // Add an object-to-subject RDF.
            $object->relationships->add($rdfNamespace, $inverseName, $subjectPid);
            // Check if symmetric object datastream processing
            // is required.
            if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
              $objectDatastreamID = $rdf_relationship['object_dsid'];
              $objectDatastream = $object[$objectDatastreamID];
              $objectDatastreamXml = $objectDatastream->content;
              add_to_datastream($subjectPid, $editFlag ? $postEditSubjectTitle : $subjectDCTitle, $rdf_relationship, $objectDatastreamXml, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build'], $rdf_relationship['object_xpath_data_build_countback'], $editFlag ? $postEditSubjectTitle : $subjectDCTitle);
              // Update the datastream with the xml containing the added node.
              $objectDatastream->content = $objectDatastreamXml;
            }
          }
        }
      }
    }
    $rdfRelationshipRdfLinkCount += 1;
  }
}

/**
 * Retrieves the pre-edit title and post-edit
 * title from the form array.
 * Serializing the entire form array and then
 * doing a string search for the relevant fields
 * is very ugly. Unfortunately there does not
 * seem to be any other way of obtaining this
 * data, because it is impossible to search a
 * multidimensional array with a string, even
 * if the string exactly mimics the equivalent
 * PHP statement that would access the desired
 * variables.

 * @param array $form
 * @param string $subjectXpathTitleSearch
 *
 * @return array
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
      $afterPreEditFieldLabel = substr($preEditTitleFieldLabelMatched, (strlen($preEditTitleFieldLabel)+1));
      $startPreEditTitleFieldDelim = strstr($afterPreEditFieldLabel, $startFieldDelim);
      if ($startPreEditTitleFieldDelim) {
        $endPreEditTitleFieldDelimPos = strpos($startPreEditTitleFieldDelim, $endFieldDelim, 1);
        if ($endPreEditTitleFieldDelimPos > 0) {
          $endPreEditTitleFieldPos = $endPreEditTitleFieldDelimPos - 1; 
          $preEditTitle = substr($startPreEditTitleFieldDelim, 1, $endPreEditTitleFieldPos);
        }
        $postEditTitleFieldLabelMatched = strstr($startPreEditTitleFieldDelim, $postEditTitleFieldLabel);
        if ($postEditTitleFieldLabelMatched) {
          $afterPostEditFieldLabel = substr($postEditTitleFieldLabelMatched, (strlen($postEditTitleFieldLabel)+1));
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
  } else {
    return NULL;
  }
}

/**
 * Escapes various characters that would otherwise
 * cause a SPARQL query to fail.
 *
 * @param string $inputStrip
 *
 * @return string
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
  } else {
    return '';
  }
}

/**
 * Performs an spo query of the triple-store and returns an array
 * of object PIDS derived from the query results.
 *
 * @param string $subject
 * @param string $predicate
 * @param string $object
 *
 * @return Array
 */
function spo_search($subject='*', $predicate='*', $object='*') {
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
       $namespacedSubjectPid = substr($elements[$subjectElement],strlen('<info:fedora/'));
       $requiredPredicateElementLen = strlen($elements[$predicateElement])-1;
       $predicateOfTriple = substr($elements[$predicateElement],1, $requiredPredicateElementLen);
       $namespacedObjectPid = substr($elements[$objectElement],strlen('<info:fedora/'));
       $processedTriples[] = array("subjectPid" => $namespacedSubjectPid, "predicate" =>  $predicateOfTriple, "objectPid" => $namespacedObjectPid);
    }
    return $processedTriples;
  } else {
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
 * @return
 */
function remove_from_datastream(&$rdf_relationship, $objectPid, $objectXpathSearch, $objectXpathBuildCountback, $elementContent) {

  $objectDatastreamID=$rdf_relationship['object_dsid'];
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
        } else {
          for ($x=1; $x<=$objectXpathBuildCountback; $x++) {
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
 * @return
 */
function replace_text_in_datastream(&$rdf_relationship, $objectPid, $objectXpathSearch, $elementContent, $replacementContent) {
  $objectDatastreamID=$rdf_relationship['object_dsid'];
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
 * @return
 */
function add_to_datastream($subjectPid, $subjectTitle, &$rdf_relationship, &$objectDatastreamXml, $objectXpathSearch, $objectXpathBuild, $objectXpathCountback, $elementContent) {
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
      if (!strcasecmp($objectItem,$elementContent)) {
        $elementContentAlreadyPresent = true;
        break;
      // Check if there is an empty element available.
      } else if (strlen($objectItem) == 0) {
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
    $objectXpathComponents = explode('/',$objectXpathBuild);
    $objectXpathComponentsTot = count($objectXpathComponents);
    $startObjectXpathComponent = $objectXpathComponentsTot - $objectXpathCountback;
    $nodePref='node';
    for ($objectXpathComponentCount=$objectXpathComponentsTot; $objectXpathComponentCount>$startObjectXpathComponent; $objectXpathComponentCount--) {
      $elementDefinition = $objectXpathComponents[$objectXpathComponentCount-1];
      $attributePhraseStart = strpos($elementDefinition, '[');
      $attributePhrase = strstr($elementDefinition, '[');
      if (strlen($attributePhraseStart) == 0) {
        ${$nodePref.$objectXpathComponentCount} = $objectDoc->createElement($elementDefinition);
      } else {
        $elementName = substr($elementDefinition,0,$attributePhraseStart);
        ${$nodePref.$objectXpathComponentCount} = $objectDoc->createElement($elementName);
        $attributePhrase = substr($attributePhrase, 1);
        $delimPos = strpos($attributePhrase, ']');
        if ($delimPos !== false) {
          $attributePhrase = substr($attributePhrase, 0, $delimPos);
          if (strlen($attributePhrase) > 0) {
            $attributeAndPresent = strpos($attributePhrase,' and ');
            if ($attributeAndPresent > 0) {
              $attributes = explode(' and ', $attributePhrase);
            } else {
              $attributes = array();
              $attributes[0] = $attributePhrase;
            }
            $attributesTot = count($attributes);
            // Attach any attributes that are present to the
            // current node.
            for ($x=0; $x<$attributesTot; $x++) {
              $attribute = explode('=', $attributes[$x]);
              $attribute[0] = trim($attribute[0]);
              if (strlen($attribute[0]) > 0) {
                $attributeTerm = substr($attribute[0],1);
                $domAttribute = $objectDoc->createAttribute($attributeTerm);
                if (strlen($attribute[1]) > 2) {
                  $attribute[1] = trim($attribute[1]);
                  $attributeValue = substr($attribute[1],1, strlen($attribute[1])-2); // Strip off quotes.
                  $domAttribute->value = $attributeValue;
                  ${$nodePref.$objectXpathComponentCount}->appendChild($domAttribute);
                }
              }
            }
          }
        }
      }
      if ($objectXpathComponentCount == $objectXpathComponentsTot) {
        $whatIsThis = $objectXpathComponents[$objectXpathComponentCount-1];
        ${$nodePref.$objectXpathComponentCount}->appendChild($objectDoc->createTextNode($elementContent));
      } else {
        $prevObjectXpathComponentCount = $objectXpathComponentCount+1;//Incrementing by one here rather than within the appendChild function is necessary for the values to concatenate correctly for the variable-variable name.
        ${$nodePref.$objectXpathComponentCount}->appendChild(${$nodePref.$prevObjectXpathComponentCount});
      }
    }

    $objectXpathStem = $objectXpathSearch;
    for ($y=0; $y<$objectXpathCountback; $y++) {
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
    $prevObjectXpathComponentCount = $objectXpathComponentCount+1;
    if ($queryResultsTot > 0) {
      $queryResult->appendChild(${$nodePref.$prevObjectXpathComponentCount});
  
      // Update the datastream with the xml containing the added node.
      $objectDatastreamXml = $objectDoc->saveXML();
    } else {
      $msg = 'While creating or editing "' . $subjectTitle . '" with ID "' . $subjectPid . '" the attempt to add a node to a symmetric datastream in a linked object failed. Check the configuration of the autocomplete record' ;
      drupal_set_message(t($msg));

    }
  }
}

/**
 *
 * Obtain the content-model-id of the object in order to
 * get the XML-Form so that it can be built to determine
 * whether there are any sparql-autocompletes attached.
 *
 * @param string $subjectPID 
 *   The PID of the object to be purged.
 *  
 * @return
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
        remove_links_on_purge($pid);
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
 * @param string $subjectPID 
 *   The PID of the object to be purged.
 *  
 * @return
 */
function remove_links_on_purge($subjectPid) {
  module_load_include('inc', 'fedora_repository', 'api/tuque');
  module_load_include('inc', 'islandora_autocomplete_sparql', 'includes/islandora_autocomplete_sparql.vocab');
  $linkTypeOutwardOnly = 0;
  $linkTypeOutwardAndInverse = 1;
  $linkTypeInverseOnly = 2;

  $rdf_relationships = variable_get('rdf_relationships', NULL);
  //variable_set('rdf_relationships', NULL); Do not set this to null here because batch purge reuses this variable when multiple objects have the same form.
  $subject = islandora_object_load($subjectPid);

  if ($subject) {
    //Get the dc.title value for the subject incase it is required
    //for removing inverse symmetric data in the object datastreams.

    $subjectDCTitle = get_dc_title($subject);
    foreach ($rdf_relationships as $rdf_relationship) {
      // The outward rdf links do not strictly need to be
      // removed as they will be deleted from the triple-store
      // as part of the standard fedora processing on purge.
      // However, as the object-datastream updates have to
      // be done and the inverse links have to be removed
      // they might as well be tidied up as we go along.
      if ($rdf_relationship['link_type'] == $linkTypeOutwardOnly || $rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
        // Store all the RDF-info relating to the
        // subject object for the current relationship
        // in an array so that the links and inverse links
        // and any appropriate datastream content in the
        // linked-to object can be removed.
        $spoSubject = '<info:fedora/' . $subjectPid . '>';
        $spoPredicate = '<' . $rdf_relationship['rdf_predicate'] . '>';
        $spoObject = '*';
        $objectTriplesForRelationship = spo_search($spoSubject, $spoPredicate, $spoObject);
        $objectTriplesForRelationshipTot = count($objectTriplesForRelationship);
        if ($objectTriplesForRelationshipTot > 0) {
          if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
            $inverseName = fba_textfield_inverseof_value($rdf_relationship['rdf_predicate']);
          }
          list($rdfNamespace, $rdfName) = fba_rdfx_split_uri($rdf_relationship['rdf_predicate']);
          foreach ($objectTriplesForRelationship as $objectTripleForRelationship) {
            $objectPid = $objectTripleForRelationship['objectPid'];
            $object = islandora_object_load($objectPid);
            if ($object) {
              if ($rdf_relationship['link_type'] == $linkTypeOutwardAndInverse) {
                if (!strcmp($inverseName, 'No inverse')) {
                  $msg = 'While purging "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" and attempting to remove the inverse link for "' . $rdfName . '" on the object "' . $objectPid . '", the inverse linkname was not found in the ontology.' ; 
                  drupal_set_message(t($msg));
                } else {
                  $object->relationships->remove($rdfNamespace, $inverseName, $subject);
                }
              }

              // Check if inverse-object-symmetric datastream processing
              // is required.
              if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
                remove_from_datastream($rdf_relationship, $objectPid, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build_countback'], $subjectDCTitle);
              }
            } else {
              $msg = 'While purging "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" and attempting to process the linkage for "' . $objectPid . '", the object for this ID was not found.' ; 
              drupal_set_message(t($msg));
            }
          }
        }
      } else {
        // This must be an inverse-only type of link.
        $inversePredicate = fba_textfield_inverseof_predicate($rdf_relationship['rdf_predicate']);
        $inverseError = false;
        if (strcmp($inversePredicate, 'No inverse predicate')) {
          list($inverseNamespace, $inverseName) = fba_rdfx_split_uri($inversePredicate);
          if (strlen($inverseName) == 0) {
            $inverseError = true;
          }
        } else {
          $inverseError = true;
        }
        if ($inverseError) {
          $msgInsert = $subjectDCTitle;
          $msg = 'While creating or editing "' . $msgInsert . '" with ID "' . $subjectPid . '" and attempting to create an inverse of the "' . $rdfName . '" link, the inverse was not found.' ; 
          drupal_set_message(t($msg));
          break;
        }

        $spoSubject = '*';
        $spoPredicate = '<' . $inversePredicate . '>';
        $spoObject = '<info:fedora/' . $subjectPid . '>';
        $objectTriplesForRelationship = spo_search($spoSubject, $spoPredicate, $spoObject);
        $objectTriplesForRelationshipTot = count($objectTriplesForRelationship);
        if ($objectTriplesForRelationshipTot > 0) {
          foreach ($objectTriplesForRelationship as $objectTripleForRelationship) {
            $objectPid = $objectTripleForRelationship['subjectPid'];
            $object = islandora_object_load($objectPid);
            if ($object) {
              $object->relationships->remove($inverseNamespace, $inverseName, $subject);
              // Check if inverse-object-symmetric datastream processing
              // is required.
              if ($rdf_relationship['symmetry_in_object_datastream_required'] && strlen($rdf_relationship['object_xpath_data_search']) > 0) {
                remove_from_datastream($rdf_relationship, $objectPid, $rdf_relationship['object_xpath_data_search'], $rdf_relationship['object_xpath_data_build_countback'], $subjectDCTitle);
              }
            } else {
              $msg = 'While purging "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" and attempting to remove the inverse link "' . $inverseName . '" on the object "' . $objectPid . '", the object for this ID was not found.' ; 
              drupal_set_message(t($msg));
            }
          }
        }
      }
    }
    // The processing above should cover all eventualities, however it  
    // is possible that inverse links remain if, for example, the
    // the sparql autocomplete records were modified after objects with
    // related links had been created. The systems administrator should 
    // not have let this situation arise, but we might as well remove
    // them if they exist. Note that what is referred to as a "subject"
    // within the spo_search and triple, is different from the main
    // "subject" of this purge, because the main "subject" is used as
    // the "object" within the spo_search.
    $spoSubject = '*';
    $spoPredicate = '*';
    $spoObject = '<info:fedora/' . $subjectPid . '>';
    $triples = spo_search($spoSubject, $spoPredicate, $spoObject);
    $triplesTot = count($triples);
    if ($triplesTot > 0) {
      foreach ($triples as $triple) {
        list($rdfNamespace, $rdfName) = fba_rdfx_split_uri($triple['predicate']);
        $pid = $triple['subjectPid'];
        $object = islandora_object_load($pid);
        if ($object) {
          $object->relationships->remove($rdfNamespace, $rdfName, $subject);
        } else {
          $msg = 'While purging "' . $subjectDCTitle . '" with ID "' . $subjectPid . '" and attempting to remove the inverse link "' . $rdfName . '" on the object "' . $pid . '", the object for this ID was not found.' ; 
          drupal_set_message(t($msg));
        }
      }
    }
  }
}

/**
 *
 * Get the contents of the Dublin Core title element
 * from the object passed in as a parameter.
 *
 * @param array $object
 *   The object from which the dc title is to be obtained.
 *  
 * @return string
 *   The Dublin Core title or an empty string
 */

function get_dc_title(&$object) {
  $DCDatastream = $object['DC'];
  $DCXml = $DCDatastream->content;
  $DCDoc = new domDocument();
  $DCDoc->loadXML($DCXml);
  $DCXpath = new domXpath($DCDoc);
  $DCXpath->registerNamespace('dc', 'http://purl.org/dc/elements/1.1/');
  $DCXpathQueryResults = $DCXpath->query('/oai_dc:dc/dc:title');

  $DCXpathQueryResultCount = 0;
  if ($DCXpathQueryResults->length > 0) {
    foreach ($DCXpathQueryResults as $DCXpathQueryResult) {
      $DCTitle = $DCXpathQueryResult->textContent;
      $DCXpathQueryResultCount += 1;
    }
  }

  if (count($DCXpathQueryResultCount > 0)) {
    return $DCTitle;
  } else {
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
 *  
 * @return string
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
  } else {
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
 *  
 * @return string
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
  } else if ($formNameTot > 1) {
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
 * @return
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
  } catch (Exception $e) {
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
 *  
 * @return
 */
function register_xpath_namespaces(&$domxpath,&$namespaceInfo,$checkPrevious=false,&$namespacePrefixesRegistered=NULL) {
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
          $namespacePrefixesRegistered[]=$namespaceComponents[0];
        }
      } else {
        $domxpath->registerNamespace($namespaceComponents[0], $namespaceComponents[1]);
      }
    } else {
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
 *  
 * @return
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
  } else {
    return '';
  }
}
/**
 *
 * Perform sparql query and return resulting array.
 *
 * @param string $sparqlQuery
 *   The sparql query
 *  
 * @return array
 *   Query results
 *   
 */
function get_link_array($sparqlQuery) {
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  global $base_url;
  $sparql_field = <<<EOQ
$sparqlQuery
EOQ;
  $limit = -1; $offset = 0;
  $queryResults = ObjectHelper::performRiQuery($sparqlQuery, 'sparql', $limit, $offset);
  if (count($queryResults) > 0) {
      return $queryResults;
  } else {
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
 *  
 * @param string $predicate
 *   The predicate for the spo search
 *  
 * @param integer $limit
 *   A limit for the sparql query
 *  
 * @param integer $returnType
 *   Determines whether an array or html-bounded list is returned
 *  
 * @param string $header
 *   If html-bounded list type is stipulated then this is the header
 *   for the list.
 *  
 * @return array
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
        $filterComponent .= 'sameTerm(?subject, <info:fedora/' .$spoResult['objectPid'] . '>)' . ' || ';
      } else {
        $filterComponent .= 'sameTerm(?subject, <info:fedora/' .$spoResult['objectPid'] . '>)' . '))';
      }
    }
    $sparqlQuery = 'SELECT ?title ?pid WHERE { ?subject <http://purl.org/dc/elements/1.1/identifier> ?pid; <http://purl.org/dc/elements/1.1/title> ?title ' . $filterComponent . ' } ORDER BY ?title';

    if ($limit > $noLimit) {
      $sparqlQuery = $sparqlQuery . ' LIMIT ' . $limit;
    }
    if ($returnType == $arrayType) {
      return get_link_array($sparqlQuery);
    } else if ($returnType == $clickableListType) {
      return get_link_list($header, $sparqlQuery, true);
    } else {
      return get_link_list($header, $sparqlQuery, false);
    }
  } else {
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
 *  
 * @param string $objectPid
 *   The object PID for the sparql query
 *  
 * @param integer $limit
 *   A limit for the sparql query
 *  
 * @param integer $returnType
 *   Determines whether an array or html-bounded list is returned
 *  
 * @param string $header
 *   If html-bounded list type is stipulated then this is the header
 *   for the list.
 *  
 * @return array
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
  $sparqlQuery = 'SELECT ?title ?pid WHERE { ?subject ' . $predicate . ' ?object;  <http://purl.org/dc/elements/1.1/title> ?title; <http://purl.org/dc/elements/1.1/identifier> ?pid ' . $filterComponent . ' } ORDER BY ?title';
  if ($limit > $noLimit) {
    $sparqlQuery = $sparqlQuery . ' LIMIT ' . $limit;
  }
  if ($returnType == $arrayType) {
    return get_link_array($sparqlQuery);
  } else if ($returnType == $clickableListType) {
    return get_link_list($header, $sparqlQuery, true);
  } else {
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
 *  
 * @param string $sparqlQuery
 *   The sparql query to be used to check for a duplicate. It
 *   should select only 'title'.
 * 
 * @return string
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
    } else {
      return 'The entry "' . $label . '" is already present within this collection, so it has not been created.';
    }
  } else {
    return NULL;
  }
}
