<?php

/**
 * @file
 *
 */
module_load_include('inc', 'fedora_repository', 'api/fedora_item');
module_load_include('inc', 'fedora_repository', 'ContentModel');
module_load_include('inc', 'fedora_repository', 'CollectionPolicy');
module_load_include('inc', 'xml_form_api', 'XMLForm');

/**
 * FIXME:  This is just a collection of functions; there is no object state.
 *   All functions should be made static and so accessed.
 */
class ManageObjectFilesForm {

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
    // SFOX NB... step 1 of this is referenced in sidora_form_alter in sidora.module, nasty hack
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
            break;
          }
        }
      case 3: {
          drupal_goto("viewer/{$pid}/overview");
          break;
        }
      default: {
          throw new Exception('Couldn\'t Create the Manage Form.');
          break;
        }
    }
  }

  /**
   * Create the first page of the Manage Files Metadata form.
   *
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   *
   * @return array
   */
  public function createPageOne($pid, $client, array &$form_state) {
    // SFOX NB... step 1 of this is referenced in sidora_form_alter in sidora.module, nasty hack
    $page = $form_state['storage']['step'] = empty($form_state['storage']['step']) ? 1 : $form_state['storage']['step'];
    switch ($page) {
      case 1: {
          return $this->createPageOneCase1($pid);
        }
      case 2: {
          $form_state['storage']['xml'] = TRUE;
          return $this->createPageTwo($pid, $client, $form_state);
        }
      case 3: {
          drupal_goto("viewer/ {
              $pid}/overview");
          break;
        }
      default: {
          throw new Exception('Couldn\'t Create the Manage Form.');
        }
    }
  }

  private function createPageOneCase1($pid) {
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    $form['markup'] = array(
      '#type' => 'markup',
      '#value' => 'Click the button above to select the csv files to be added to this dataset.<br/>',
    );
    $pidprefix = substr($pid, 0, strpos($pid, ':'));
    $collection_pid = fba_solution_pack_get_cmodel_from_namespace($pidprefix);
    $form['collection_pid'] = array(// This is required to ensure the correct javascript Next/Finish button is chosen
      '#type' => 'hidden',
      '#value' => $collection_pid,
    );
    $form['submit'] = array(
      '#type' => 'submit',
      '#submit' => array('sidora_batch_manage_form_submit'),
      '#value' => 'Next'
    );
    return $form;
  }

  public function createPageTwo($pid, $client, array &$form_state) {
    //This will say the file has been uploaded and saved
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    $form['markup'] = array(
      '#type' => 'markup',
      '#value' => 'CSV all uploaded now<br/>',
    );
    return $form;
  }

  /**
   * Submit.
   *
   * @param array $form
   * @param array $form_state
   */
  public function submit(array &$form, array &$form_state) {
    module_load_include('inc', 'sidora', 'includes/fbasidora/common');
    //The file has been uploaded and now needs adding to the data component
    $form_state['storage'] = NULL;
    $pid = $form['#parameters']['2'];
    $form_state['redirect'] = "viewer/ {$pid}/overview";
    submit_uploaded_files($form, $form_state);
  }

}

/**
 * Handles the submission of the manage form.
 *
 */
function sidora_batch_manage_form_submit(array $form, array &$form_state) {
  module_load_include('inc', 'islandora_content_model_forms', 'ManageObjectFilesForm');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');

  $isWizard = $_POST['wizard_check'] === 'true';
  if ($form_state['storage']['step'] == 2) {
    // Ignore normal ingest messages, no idea why this is here, oh well.
    unset($_SESSION['messages']);
    // Needs to be called before $ingest_form->submit() as it
    // removes the $form_state['storage']
    $sessionid = $_SESSION['sidora_file_sessionid'];
    $files = $_SESSION['file_uploads'][$sessionid];
    //$files = $_SESSION['file_uploads'][$form_state['values']['form_build_id']]; 
    $preserved_form_state = $form_state;
    $parent = islandora_object_load($form_state['values']['collection_pid']);
    $_SESSION['pid_list'] = array(); //Wizard will pop pids off of this
    $_SESSION['orig_pid_list'] = array(); //Original set of pids for this action (used for delete / purge on cancel)
    if (!empty($_SESSION['codebook_created'])) {
      array_push($_SESSION['orig_pid_list'], $_SESSION['codebook_created']);
    }
    $_SESSION['codebook_created'] = ''; //Clear out if a codebook has been created for this add resources
    //If the add resources gets cancelled we need to delete a codebook (if it's been created)
    //codebook_created will only be filled in if the codebook was created in the most recent add resources

    if (empty($files)) {
      $child = islandora_object_load($form_state['values']['pid']);
      if ($child->relationships) {
        // Remove the default relationship.
        $child->relationships->remove(RELS_EXT_URI, 'isMemberOfCollection', $parent->id);
      }
      $child->models = sidora_is_concept($child) ? 'si:conceptCModel' : 'si:resourceCModel';
      if (sidora_is_resource_type($child, 'si:codebookCModel')) {
        //Put codebooks in the special codebook area, this is a NEW codebook for the current click of Add Resource
        $_SESSION['codebook_created'] = $child->id;
        sidora_add_child(sidora_get_codebook_holder_for_user(), $child);
      }
      else {
        // Add sidora specific relationships isResourceOf/hasResource, etc.
        sidora_add_child($parent, $child);
      }
      // Mark metadata as original, affects the display of resources only.
      if ($child->relationships) {
        $child->relationships->add('http://islandora.org/ontologies/metadata#', 'orginal_metadata', 'TRUE', TRUE);
      }
      // Concepts need collection policies to ingest.
      if (sidora_is_concept($child)) {
        $collection_policy = drupal_get_path('module', 'sidora') . '/data/policies/default_COLLECTION_POLICY.xml';
        sidora_update_datastream($child, $collection_policy, 'COLLECTION_POLICY', NULL, 'text/xml', 'X');
      }
      // Add default XACML policy.
      sidora_update_datastream($child, sidora_create_default_xacml(), 'POLICY', NULL, 'text/xml');
      // If no files let the user know of the success.
      drupal_set_message(t("Successfully added @object to @parent", array('@object' => $child->id, '@parent' => $parent->id)));
    }
    else { //NOT WIZARD, YES FILES
      $pid = $form['#parameters']['2'];
      // Sidora custom modifications.
      $child = islandora_object_load($pid);
      // If no files let the user know of the success.
      if (empty($files)) {
        drupal_set_message(t("Successfully added @object to @parent", array('@object' => $child->id, '@parent' => $parent->id)));
      }
      $lastobjnumber = 0;
      foreach ($child as $datastream) {
        $dsid = $datastream->id;
        if (substr_compare($dsid, 'OBJ', 0, 3) === 0) { //starts with OBJ
          $objnumber = (int) substr($dsid, 3);
          if ($objnumber > $lastobjnumber) {
            $lastobjnumber = $objnumber;
          }
        }
      }
      $numberoffiles = count($files);
      $i = $lastobjnumber + 1;
      // Generate the multiple objects required for each uploaded file.

      $batch = array(
        'title' => t('Processing ...'),
        'file' => drupal_get_path('module', 'islandora_content_model_forms') . '/ManageObjectFilesForm.inc',
        'operations' => array(), // Operations to complete, in order. Defined below.
        'finished' => 'completeDeleteObjectDatastreamsProcessing',
        'init_message' => t('Initialising...'),
        'progress_message' => t('Processed @current out of @total.'),
        'error_message' => t('An error was encountered while processing links.'),
      );
      foreach ($files as $file) {
        //Need to find the latest OBJ datastream and then add after that
        drupal_write_record('files', $file);
        try {
          if (ereg('.csv$', $file->filename)) { //csvs always have a suffix
            $dsidtext = 'OBJ' . ($i);
          }
          else { //this is for non csv's
            if (count($files) == 1) { //only one file, so don't suffix with a number
              $dsidtext = 'OBJ';
            }
            else {
              $dsidtext = 'OBJ' . ($i);
            }
          }
          //Create the batch queue of these 2 functions separately
          $batch['operations'][] = array('batch_sidora_update_datastream', array($child, $file->filepath, $dsidtext, $file->filename));
          $batch['operations'][] = array('create_object_datacomponent_datastreams', array($pid, $dsidtext));
        }
        catch (Exception $e) {
          echo 'Caught exception: ', $e->getMessage(), "\n";
          file_save_data(print_r($e->getMessage(), TRUE), '/tmp/log_update_ds.txt', FILE_EXISTS_RENAME);
        }
        $i++;
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

    if ($isWizard) {
      $_SESSION['current_pid'] = array_pop($_SESSION['pid_list']);
    }
  }
}

function create_object_datacomponent_datastreams($pid, $objdsid, &$context) {
  module_load_include('module', 'tuque_wrapper', 'tuque_wrapper');

  //This function (as opposed to the repair function) doesn't recreate the COLS 
  //datastreams for existing OBJ datastreams, or redoes the rels int links
  $repository = tuque_wrapper_get_repository_instance();
  $repo_object = $repository->getObject($pid);
  $colsds_content = array(); //array of column datastreams for later ingestion
  foreach ($repo_object as $datastream) {
    $dsids[] = $datastream->id;
  }
  //starts with OBJ
  //if the matching COLS datastream exists, then don't recreate it
  //Assumption, if one COLS exists, then the full amount of COLS datastreams do too.
  $dsidnumber = substr($objdsid, 3); //just get the datastream number (e.g. ignoring the 'OBJ' bit)
  //The COLS datastreams don't exist, so it's ok to create them
  $objdatastream = $repository->api->a->getDatastreamDissemination($repo_object->id, $objdsid, NULL); // Work around for bug in Tuque API: ISLANDORA-752
  // Processing First Row of CSV file (user defined headers)
  // Extract to an array containing each header
  $headers = trim_array_values(explode(",", strtok($objdatastream, "\n")));
  // Processing Second Row of CSV files (vocab terms)
  // Extract to an array containing each vocab_term then convert to DTC vocab pid
  $vocab_terms = trim_array_values(explode(",", strtok("\n")));
  // NB TODO replace the use of explode() with fgetcsv (in case cells have commas in them)

  for ($i = 0, $j = count($vocab_terms); $i < $j; $i++) {
    $term = trim($vocab_terms[$i]);
    // CSV file has already been validated using the check_vocab_term function, so
    // a vocab pid will be returned for each element in vocab_terms
    $vocab_terms[$i] = _check_vocab_term(&$term);
  }

  // Processing Third Row of CSV files (units)
  // Extract the third row information into an array
  // This can contain Unit: and Taxon: and these are delimited by ~~ 
  // (possibly with whitespace before/after)
  $units = trim_array_values(explode(",", strtok("\n")));
  foreach ($units as $key => $unitterm) {
    $termarray = explode('~~', $unitterm);
    $unitcount[$fileindex][$key] = 0;
    foreach ($termarray as $termarrayvalue) {
      $colonpos = strpos($termarrayvalue, ':');
      $termprefix = trim(substr($termarrayvalue, 0, $colonpos));
      switch ($termprefix) {
        case 'Units': {
            $unitcount[$fileindex][$key] ++;
            $termvalue = trim(substr($termarrayvalue, $colonpos + 1));
            //N/A is a valid unit but isn't in the vocabulary
            if (strcmp($termvalue, 'N/A') === 0) {
              //$checked_unit_result = 'N/A';
              $units[$key] = 'N/A';
            }
            else {
              //$checked_unit_result = _check_vocab_term(&$termvalue);
              $unit = _get_units($termvalue);
              $units[$key] = _check_vocab_term($unit);
            }
            break;
          }
        case 'Taxon': {
            $taxonvalue = trim(substr($termarrayvalue, $colonpos + 1));
            $taxon = _get_taxon($taxonvalue);
            $checkedtaxonterm = _check_taxon_term($taxon);
            if (is_null($checkedtaxonterm)) {
              //Need to create an object for this as it doesn't yet exist
              $scientificName = $taxonvalue;
              $namespace = 'fwltaxon';
              $collection_pid = 'info:fedora/' . $namespace . ':collection';
              $nextavailablepid = fedora_item::get_next_PID_in_namespace($namespace);
              global $user;
              $item = fedora_item::ingest_new_item($nextavailablepid, 'A', $scientificName, $user->name);
              $item->add_relationship('isMemberOfCollection', $collection_pid, RELS_EXT_URI);
              $item->add_relationship('hasModel', 'fba:taxon_cmodel', FEDORA_MODEL_URI);
              $dwcdsid = 'DWC';
              $UTCdateformatstring = 'Y-m-d\TH:i:s'; // need to append '.\000Z';                                    
              $reccreDateTime = new DateTime();
              //Translate to YYYY-MM-DDTHH:MM:SS.uuuZ
              $reccresolrDateTime = $reccreDateTime->format($UTCdateformatstring) . '.000Z';

              // PHP tries to be clever if it detects http colon slash slash in a string so it
              // it necessary to break up the namespacing into segments.
              $DWC1 = '<DarwinRecordSet xmlns="ht' .
                  'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:dcterms="' . PURL_TERMS_URI .
                  '" xmlns:dwr="ht' .
                  'tp://rs.tdwg.org/dwc/dwcrecord" xmlns:xsi="ht' .
                  'tp://www.w3.org/2001/XMLSchema-instance" xmlns:dwc="ht' .
                  'tp://rs.tdwg.org/dwc/terms/">';

              $DWC2 = sprintf('<dwc:Taxon><dwc:taxonID></dwc:taxonID><dwc:parentNameUsage></dwc:parentNameUsage><dwc:parentNameUsageID></dwc:parentNameUsageID><dwc:taxonRank></dwc:taxonRank><dwc:scientificName>%s</dwc:scientificName><dwc:scientificNameAuthorship></dwc:scientificNameAuthorship><dwc:namePublishedIn></dwc:namePublishedIn><dwc:nomenclaturalCode></dwc:nomenclaturalCode><dwc:taxonomicStatus></dwc:taxonomicStatus><dwc:vernacularName></dwc:vernacularName><dwc:acceptedNameUsage></dwc:acceptedNameUsage><dwc:acceptedNameUsageID></dwc:acceptedNameUsageID><dwc:originalNameUsage></dwc:originalNameUsage><dwc:originalNameUsageID></dwc:originalNameUsageID><dwc:taxonRemarks></dwc:taxonRemarks><dcterms:modified>%s</dcterms:modified></dwc:Taxon></DarwinRecordSet>', $scientificName, $reccresolrDateTime);

              $DWCDatastream = $DWC1 . $DWC2;

              //need to convert the date formats from 2013-03-12 to 2013-03-12T00:00:00.000Z

              $item->add_datastream_from_string($DWCDatastream, $dwcdsid, 'DWC Record', 'text/xml', 'X');

              $proc = new XsltProcessor();

              $xsl = new DomDocument();
              $path = drupal_get_path('module', 'islandora_content_model_forms');
              $xsl->load($path . '/transforms/dwc_taxon_to_dc.xsl');
              $input = new DomDocument();
              $input->loadXML(trim($DWCDatastream));
              $proc->importStylesheet($xsl);
              $newdom = $proc->transformToDoc($input);
              $dc_xml = $newdom->saveXML();
              $item->add_datastream_from_string($dc_xml, 'DC', 'DC', 'text/xml', 'X');
              $taxa[$key][] = 'info:fedora/' . $item->pid;
            }
            else {
              $cttxml = $checkedtaxonterm->asXML();
              $taxa[$key][] = substr($cttxml, strpos($cttxml, '"') + 1, -1);
            }
            break;
          }
        default: {
            break;
          }
      }
    }
    //put some flag in here about valid csv?
  }
  // go through and extract the data into an array of arrays
  $current_row = explode(",", strtok("\n"));
  $data_array = array();
  while (count($current_row) > 1 && $current_row[0] !== NULL) {
    // add new data to the data array
    $data_array[] = $current_row;
    $current_row = explode(",", strtok("\n"));
  };


  // 5. Generate COLSn-n datastream [see later functions]
  $numberofcolumns = count($headers);
  for ($colindex = 0; $colindex < $numberofcolumns; $colindex++) {
    $coldsname = 'COLS' . $dsidnumber . '-' . ($colindex + 1); //+1 as loop starts at 0
    $paramvocabterm = $paramunit = $paramtaxa = NULL;
    $paramheader = $headers[$colindex];
    if ($vocab_terms[$colindex]) {
      $paramvocabterm = (string) $vocab_terms[$colindex];
    }
    if ($units[$colindex]) {
      $paramunit = (string) $units[$colindex];
    }
    if ($taxa[$colindex]) {
      $paramtaxa = $taxa[$colindex];
    }
    $colsds_content[$coldsname] = _create_column_datastream($colindex, $paramheader, $paramvocabterm, $paramunit, $paramtaxa, $data_array, $pid, $objdsid);
  }
  //create the COLSxx-y datastreams and the RELS-INT links for the new OBJxx datastream
  $objsrelations = $repo_object[$objdsid]->relationships;
  foreach ($colsds_content as $columnname => $colds) {
    $cols_datastream = _create_datastream($repo_object, $columnname, 'X', 'Fedora Internal Relationships file for this object', 'application/rdf+xml', $colds->asXML());
    $repo_object->ingestDatastream($cols_datastream);
    $objsrelations->add('info:fedora/fedora-system:def/relations-internal#', 'hasConstituent', $pid . '/' . $columnname, RELS_TYPE_URI);
  }

  unset($colsds_content);
}

function batch_sidora_update_datastream($child, $file, $dsidtext, $filename) {
  sidora_update_datastream($child, $file, $dsidtext, $filename, NULL, 'M', FALSE);
}
