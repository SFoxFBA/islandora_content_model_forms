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
 * Create an ingest form.
 * FIXME:  This is just a collection of functions; there is no object state.
 *   All functions should be made static and so accessed.
 */
class IngestObjectMetadataForm {

  /**
   * Get Content Models for the form.
   *
   * @param string $collection_pid
   *
   * @return array
   */
  public function getPotentialContentModels($collection_pid, array &$form_state) {
    $potential_models = array();

    $collection_policy = CollectionPolicy::loadFromCollection($collection_pid);
    $content_models = $collection_policy->getContentModels();

    //Add models from collection policy.
    if (($collection_policy = CollectionPolicy::loadFromCollection($collection_pid)) !== FALSE && ($content_models = $collection_policy->getContentModels())) {
      $potential_models = array();
      foreach ($content_models as $content_model) {
        $identifier = $content_model->getIdentifier();
        $name = $content_model->name;
        $potential_models["$identifier"] = "$name";
      }
    }

    // Add cmodels to programatically include...
    if ($CMs = $form_state['storage']['content_model']) {
      $CMs = (array) $CMs;
      module_load_include('inc', 'fedora_repository', 'ContentModel');
      foreach ($CMs as $content_model) {
        $content_model = ContentModel::loadFromModel($content_model);
        $identifier = $content_model->getIdentifier();
        $name = $content_model->name;
        $potential_models["$identifier"] = "$name";
      }
    }

    return $potential_models;
  }

  /**
   * Create the first page of the Ingest Object Metadata form.
   *
   * @note
   *   this code contains a temporary fix and should be replaced as soon as a more permananent solution is developed
   * currently if a collection has more than one content model listed in the collection policy the user
   * will only have access to the first form associated with the content type.
   *
   * @param string $collection_pid
   * @param string $collection_label
   * @param array $form_state
   *
   * @return array
   */
  public function createPageOne($collection_pid, $collection_label, array &$form_state) {

    $potential_models = $this->getPotentialContentModels($collection_pid, $form_state);
    unset($potential_models['islandora:collectionCModel/ISLANDORACM']);
    list($identifier, $name) = array_peek($potential_models);
    $selected_model = isset($form_state['values']['models']) ? $form_state['values']['models'] : $identifier;
    $identifier = isset($form_state['post']['models']) ? $form_state['post']['models'] : $identifier;
    $content_model_pid = ContentModel::getPidFromIdentifier($identifier);
    $form_names = islandora_content_model_get_form_names($content_model_pid);

    $form = array();

    if (count($potential_models) == 0) {
      if (!user_access('manage collections')) {
        $message = t('This collection cannot receive additions.  Please contact the site administrator if you believe this message to be in error.');
      }
      else {
        if (module_exists('islandora_collection_manager')) {
          // XXX: Should get the value from the collection manager?
          $message = t('Select the %manage tab to add content models or child collections to this collection', array('%manage' => 'Manage This Collection'));
        }
        else {
          $collection_link = l("Islandora Collection Manager", "https://wiki.duraspace.org/display/ISLANDORA6121/Collection+Manager");
          $message = t('This collection requires either one or more content models in its COLLECTION_POLICY, or one or more child collections.<br />
                      We strongly recommend enabling the !collection_link module for managing collection objects.', array('!collection_link' => $collection_link));
        }
      }
      $form['message'] = array(
        '#type' => 'item',
        '#value' => $message,
      );
      return $form;
    }

    $form['indicator'] = array(
      '#type' => 'fieldset',
      '#attributes' => array('class' => 'fedora_ingester'),
      '#id' => 'fedora_ingest_fieldset',
      //DTC-198 removed pid '#title' => t('Ingest digital object into %collection_label (@collection_pid), Step #1:', array('@collection_pid' => $collection_pid, '%collection_label' => $collection_label)),
      '#title' => t('Select the type and then click on the "Next" button:'),
    );

    $form['indicator']['models'] = array(
      '#type' => 'select',
      '#title' => t('Data Component Type'),
      '#options' => $potential_models,
      '#default_value' => $selected_model,
      '#description' => t(''),
      '#ahah' => array(
        'path' => 'islandora/form/update',
        'wrapper' => 'content-form-choice',
        'effect' => 'fade',
      ),
    );

    $form['indicator']['content_form_ahah_wrapper'] = array(
      '#prefix' => '<div id="content-form-choice">',
      '#suffix' => '</div>',
      '#type' => 'item',
    );

    if (count($form_names) == 1) {
      $form['indicator']['content_form_ahah_wrapper']['forms'] = array(
        '#type' => 'hidden',
        '#value' => array_shift(array_keys($form_names))
      );
    }
    elseif (count($form_names) > 0) {
      $form['indicator']['content_form_ahah_wrapper']['forms'] = array(
        '#type' => 'select',
        '#title' => t('Select form'),
        '#options' => $form_names,
        '#description' => t('Select the form to populate the metadata of the new object.'),
      );
    }

    $form['collection_pid'] = array(
      '#type' => 'hidden',
      '#value' => $collection_pid
    );
    $form['submit'] = array(
      '#type' => 'submit',
      '#submit' => array('fedora_repository_ingest_form_submit'),
      '#value' => t('Next'),
    );
    return $form;
  }

  /**
   * Removes all required fields
   */
  public function _islandora_content_model_forms_remove_required(&$element) {
    $mandatoryvalue = $element['#attributes']['mandatory'];
    if (strcmp($mandatoryvalue, '1') === 0) {
      //Need to add red asterisk to the title, and then disable required field checking
      $title = $element['#title'] . '<span class="form-required" title="This field is required.">*</span>';
      $element['#title'] = $title;
      $element['#required'] = FALSE;
    }

    foreach (element_children($element) as $key) {
      $this->_islandora_content_model_forms_remove_required($element[$key]);
    }
  }

  /**
   * Create the second page of the Ingest Object Metadata form.
   *
   * @param string $collection_pid
   * @param string $collection_label
   * @param array $form_state
   *
   * @return array
   */
  public function createPageTwo($collection_pid, $collection_label, array &$form_state) {
    module_load_include('inc', 'fedora_repository', 'formClass');
    $path = drupal_get_path('module', 'islandora_content_model_forms');
    $content_model_pid = ContentModel::getPidFromIdentifier($form_state['values']['models']);
    $content_model_dsid = ContentModel::getDSIDFromIdentifier($form_state['values']['models']);

    $form_name = NULL;
    if (isset($form_state['values']['forms'])) {
      $form_name = $form_state['values']['forms'];
    }
    else {
      $form_names = islandora_content_model_get_form_names($content_model_pid);
      reset($form_names);
      $form_name = key($form_names);
      $form_state['values']['forms'] = $form_name;
    }
    if (empty($form_name)) {
      $ingestForm = new formClass();
      $form_state['storage']['content_model'] = $content_model;
      $form_state['storage']['collection_pid'] = $collection_pid;
      return $ingestForm->createQDCIngestForm($collection_pid, $collection_label, $form_state);
    }
    $dsid = islandora_content_model_get_dsid($content_model_pid, $form_state['values']['forms']);

    $xml = NULL;
    if (!empty($form_state['storage']['xml_data'])) {
      $xml = $form_state['storage']['xml_data'];
    }

    $form = xml_form_builder_get_form($form_state, $form_name, $xml);
    //SFOX This has been customised to signify a required field with asterisk
    //but then deactivating the required flag, so that the form doesn't handle this validation.
    //The mandatory field validation is done just before the publish process completion.
    $this->_islandora_content_model_forms_remove_required($form);
    $form['collection_pid'] = array(
      '#type' => 'hidden',
      '#value' => $collection_pid
    );
    $form['content_model_pid'] = array(
      '#type' => 'hidden',
      '#value' => $content_model_pid
    );
    $form['content_model_dsid'] = array(
      '#type' => 'hidden',
      '#value' => $content_model_dsid
    );
    $form['dsid'] = array(
      '#type' => 'hidden',
      '#value' => $dsid
    );
    $form['models'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['models']
    );
    $form['forms'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['forms']
    );
    $form['submit'] = array(
      '#type' => 'submit',
      '#attributes' => array('class' => 'fedora_ingest_page_two_submit'),
      '#submit' => array('fedora_repository_ingest_form_submit'),
      '#value' => t('Submit'),
    );
    ob_start();
    var_dump($form);
    $result = ob_get_clean();


    return $form;
  }

  /**
   * Create the second page of the Ingest Object Metadata form.
   *
   * @param string $collection_pid
   * @param string $collection_label
   * @param array $form_state
   *
   * @return array
   */
  public function createPageThree($collection_pid, $collection_label, array &$form_state) {
    module_load_include('inc', 'fedora_repository', 'formClass');
    $path = drupal_get_path('module', 'islandora_content_model_forms');
    $content_model_pid = ContentModel::getPidFromIdentifier($form_state['values']['models']);
    $content_model_dsid = ContentModel::getDSIDFromIdentifier($form_state['values']['models']);

    $form_name = NULL;
    if (isset($form_state['values']['forms'])) {
      $form_name = $form_state['values']['forms'];
    }
    else {
      $form_names = islandora_content_model_get_form_names($content_model_pid);
      reset($form_names);
      $form_name = key($form_names);
      $form_state['values']['forms'] = $form_name;
    }
    if (empty($form_name)) {
      $ingestForm = new formClass();
      $form_state['storage']['content_model'] = $content_model;
      $form_state['storage']['collection_pid'] = $collection_pid;
      return $ingestForm->createQDCIngestForm($collection_pid, $collection_label, $form_state);
    }
    $dsid = islandora_content_model_get_dsid($content_model_pid, $form_state['values']['forms']);

    $xml = NULL;
    if (!empty($form_state['storage']['xml_data'])) {
      $xml = $form_state['storage']['xml_data'];
    }

    $form = xml_form_builder_get_form($form_state, $form_name, $xml);
    $form['collection_pid'] = array(
      '#type' => 'hidden',
      '#value' => $collection_pid
    );
    $form['content_model_pid'] = array(
      '#type' => 'hidden',
      '#value' => $content_model_pid
    );
    $form['content_model_dsid'] = array(
      '#type' => 'hidden',
      '#value' => $content_model_dsid
    );
    $form['dsid'] = array(
      '#type' => 'hidden',
      '#value' => $dsid
    );
    $form['models'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['models']
    );
    $form['forms'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['forms']
    );
    $form['submit'] = array(
      '#type' => 'submit',
      '#attributes' => array('class' => 'fedora_ingest_page_two_submit'),
      '#submit' => array('fedora_repository_ingest_form_submit'),
      '#value' => t('Submit'),
    );

    return $form;
  }

  /**
   * Create the Ingest Object Metadata form.
   *
   * @param string $collection_pid
   * @param string $collection_label
   * @param array $form_state
   *
   * @return array
   */
  public function create($collection_pid, $collection_label, array &$form_state) {
    $page = $form_state['storage']['step'] = empty($form_state['storage']['step']) ? 1 : $form_state['storage']['step'];

    if ($page == 1) {
      $form_state['storage']['xml'] = TRUE;
      return $this->createPageOne($collection_pid, $collection_label, $form_state);
    }
    else if ($form_state['storage']['xml']) {
      return $this->createPageTwo($collection_pid, $collection_label, $form_state);
    }
    else {
      throw new Exception("Couldn't Create the Ingest Form.");
    }
  }

  /**
   * Ingest the object.
   *
   * @global string $base_url
   *
   * @param array $form
   * @param array $form_state
   */
  public function submit(array &$form, array &$form_state) {
    global $base_url;
    if (isset($_FILES['files']['name']['file_upload']) && $_FILES['files']['name']['file_upload'] == 'text/xml') {
      $tmpDir = file_directory_path();
      if (!$file = file_save_upload('file_upload', NULL, $tmpDir)) {
        drupal_set_message('No file loaded', 'warning');
        return;
      }
      $file = "$tmpDir/{$file->filename}";
      $mods_xml = file_get_contents($file);
      $document = DOMDocument::loadXml($mods_xml);
      file_delete($file);
      if (!$document) {
        drupal_set_message('This does not appear to be a valid XML file', 'warning');
        return;
      }
    }
    else {
      $xml_form = new XMLForm($form_state);
      $doc = $xml_form->submit($form, $form_state);
      $document = $doc->document;
    }

    $collection_pid = $form_state['values']['collection_pid'];
    $content_model_pid = $form_state['values']['content_model_pid'];
    $content_model_dsid = $form_state['values']['content_model_dsid'];
    $dsid = $form_state['values']['dsid'];
    $form_name = $form_state['values']['forms'];
    $should_redirect = TRUE;

    $relationship = NULL;
    $pid = NULL;
    //SFOX add object to user's private collection
    $pidnamespace = fba_solution_pack_get_namespace_from_cmodel($content_model_pid);
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    $pid = Fedora_Item::get_next_PID_in_namespace($pidnamespace);
    $relationship = 'isMemberOfCollection';


    $mods_nodelist = $document->getElementsByTagName('mods');
    if ($mods_nodelist->length > 0) {
      $mods_element = $mods_nodelist->item(0);
      $identifier_element = $document->createElement('identifier', $pid);
      $identifier_element->setAttribute('type', 'local');
      $mods_element->appendChild($identifier_element);
    }

    if (empty($relationship)) {
      if (!empty($form_state['storage']['parent_relation'])) {
        $relationship = $form_state['storage']['parent_relation'];
      }
    }

    if (!empty($relationship) && !empty($pid)) {
      module_load_include('inc', 'islandora_content_model_forms', 'FOXML');
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
//      eval('$label = $form_state[\'values\']' . $label_field . ';'); //FIXME:  This doesn't really work in many cases (building the title from multiple fields, as for a person's last and first name fields.  Also:
      //FIXME:  'eval' is terrible... Get rid of it!
      if (empty($label)) {
        $label = "Undefined";
      }
      if (empty($form['#post']['active-toggle'])) {
        $state = 'A';
      }
      else {
        $state = $form['#post']['active-toggle'] ? 'A' : 'I';
      }

      //SFOX archival version number needs populating (0 for private version)
      $docelval = $document->getElementsByTagName('archivalPublicationVersion');
      $docelval->item(0)->nodeValue = 0;
      //
      $form_state['values']['pid'] = $pid;
      // added ne functionality to static thumbnails to objects if values are provided in forms
      $module = isset($form_state['values']['module']) ? $form_state['values']['module'] : NULL;
      $image_dir = isset($form_state['values']['image_directory']) ? $form_state['values']['image_directory'] : NULL;
      $thumbnail_file = isset($form_state['values']['thumbnail_file']) ? $form_state['values']['thumbnail_file'] : NULL;

      if ($module && $thumbnail_file) {
        $stored_thumbnail = drupal_get_path('module', $module) . "/$image_dir/$thumbnail_file";
        $stored_thumbnail = preg_replace('/\/\//', '/', $stored_thumbnail);
        file_copy($stored_thumbnail, file_directory_path());
        $thumbnail = file_directory_path() . '/' . $thumbnail_file;
        $_SESSION['fedora_ingest_files']['TN'] = $thumbnail;
      }

      $uploaded_file = isset($form_state['values']['ingest-file-location']) && trim($form_state['values']['ingest-file-location']) !== '';
      $ingest_file_location = $uploaded_file ? $form_state['values']['ingest-file-location'] : NULL;
      if (isset($ingest_file_location)) {
        $ingest_file_location = is_array($ingest_file_location) ? $ingest_file_location : array('OBJ' => $ingest_file_location);
      }
      if (isset($_SESSION['fedora_ingest_files']) && is_array($_SESSION['fedora_ingest_files'])) {
        foreach ($_SESSION['fedora_ingest_files'] as $key => $value) {
          $ingest_file_location[$key] = $value;
        }
      }

      $transform = islandora_content_model_get_transform($content_model_pid, $form_name);

      /**
       *
       * FBA MODIFICATION BY NBYWELL on 06/03/2014. Check content of
       * the 'rdf_relationships' variable which triggers the
       * Object Linker functionality.
       *
       */
      module_load_include('inc', 'islandora_content_model_forms', 'ObjectLinker');
      $duplicateEntryMsg = NULL;

      /**
       *
       * This is a check to determine if an entry already exists
       * in the current collection.
       */
      switch ($collection_pid) {
        // List of collections in which duplicates are not permitted.
        case "fba:vocabpref_collection":
        case "fba:vocabnonpref_collection":
        case "fba:geopref_collection":
        case "fba:geononpref_collection": {
            $sparqlQuery = 'SELECT ?title ?pid WHERE { ?pid <http://purl.org/dc/elements/1.1/title> ?title; <fedora-model:state> <fedora-model:Active>; <fedora-rels-ext:isMemberOfCollection> <info:fedora/' . $collection_pid . '> FILTER regex(str(?title), "^' . $label . '$", "i") } LIMIT 10';
            $duplicateEntryMsg = check_for_duplicate_label($label, $sparqlQuery);
            break;
          }
        default: {
            
          }
      }

      if ($duplicateEntryMsg) {
        drupal_set_message($duplicateEntryMsg);
      }
      else {
        if (variable_get('rdf_relationships', NULL)) {
          preprocess_link_data($document);
        }
        $foxml = new FOXML($label, $pid, $dsid, $content_model_pid, $collection_pid, $relationship, $ingest_file_location, $document, $transform, $state);
        $foxml->ingest();
        /* SFOX need to blank out all dc datastream values for now, so that they don't get indexed ?? */
       
        if (variable_get('rdf_relationships', NULL)) {
          process_links($form, $form_state, false);
        }
        $_SESSION['fedora_ingest_files'] = '';
        $form_state['storage'] = NULL;
      }
    }
    $form_state['redirect'] = "fedora/repository/{$collection_pid}";
  }
}

function islandora_content_model_update_form_choice() {
  // prep only
  $form_state = array('storage' => NULL, 'submitted' => FALSE);
  $form_build_id = $_POST['form_build_id'];
  $form = form_get_cache($form_build_id, $form_state);
  $args = $form['#parameters'];
  $form_id = array_shift($args);
  $form_state['post'] = $form['#post'] = $_POST;
  // Enable the submit/validate handlers to determine whether AHAH-submittted.
  $form_state['ahah_submission'] = TRUE;
  $form['#programmed'] = $form['#redirect'] = FALSE;
  drupal_process_form($form_id, $form, $form_state);
  $form = drupal_rebuild_form($form_id, $form_state, $args, $form_build_id);

  $changed_elements = $form['indicator']['content_form_ahah_wrapper'];
  unset($changed_elements['#prefix'], $changed_elements['#suffix']);
  $output = theme('status_messages') . drupal_render($changed_elements);
  drupal_json(array(
    'status' => TRUE,
    'data' => $output,
  ));
}
