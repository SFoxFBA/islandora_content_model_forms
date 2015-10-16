<?php

/*
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
class DeleteObjectFilesForm {

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
          break;
        }
      case 3: {
          if ($form_state['storage']['xml']) {
            return $this->createPageThree($pid, $client, $form_state);
          }
          break;
        }
      case 4: {
          drupal_goto("viewer/{$pid}/overview");
        }
    }
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
    $result_object = islandora_object_load($pid);
    if ($result_object) {
      $numberofcsvs = 0;
      foreach ($result_object as $datastream) {
        if (preg_match('/^OBJ/', $datastream->id)) {
          $objds = $result_object[$datastream->id];
          $objlabels[$numberofcsvs] = $datastream->label;
          $objdsids[$numberofcsvs] = $datastream->id;
          $numberofcsvs++;
        }
      }
    }
    $form['plaintext'] = array(
      '#type' => 'markup',
      '#value' => 'Attached csv filenames:<br/>',
    );
    $rownumber = 0;
    for ($i = 0; $i < $numberofcsvs; $i++) {
      $rownumber++;
      $form['row' . $rownumber] = array(
        '#type' => 'fieldset',
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['row' . $rownumber]['dslabel'] = array(
        '#type' => 'markup',
        '#value' => 'CSV Filename:' . $objlabel . '<br/>',
      );
      $form['row' . $rownumber]['checkbox'] = array(
        '#type' => 'checkbox',
        //'#value' => $objdsids[$i] . '-' . $objlabels[$i],
        '#title' => $objlabels[$i],
        '#name' => 'objs' . $rownumber,
        '#return_value' => check_plain($objdsids[$i] . '-' . $objlabels[$i]),
        '#parents' => array('row' . $rownumber),
      );
    }
    if ($rownumber > 0) {
      $form['submit'] = array(
        '#type' => 'submit',
        '#value' => 'Submit',
      );
    }
    else {
      $form['plaintext'] = array(
        '#type' => 'markup',
        '#value' => 'No attached CSV files.<br />',
      );
      $form['noattachedcsvs'] = array(
        '#type' => 'hidden',
        '#value' => TRUE,
      );
    }

    $form['fba-workflow'] = array(
      '#type' => 'hidden',
      '#value' => 'delete',
    );
    $form['fba-page'] = array(
      '#type' => 'hidden',
      '#value' => 1,
    );

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
    $datastreamstodelete = $form_state['values']['dstodelete'];
    $item = new Fedora_Item($pid);

    foreach ($datastreamstodelete as $datastreamtodelete => $dslabel) {
      $htmlmarkup .= ' - ' . $dslabel . '<br/>';
      $formds .= $datastreamtodelete . '|';
    }
    $form['markup'] = array(
      '#type' => 'markup',
      '#value' => 'Are you sure you want to delete these attachments?:<br/>' . $htmlmarkup,
    );
    $form['dstodelete'] = array(
      '#type' => 'hidden',
      '#value' => $formds,
    );
    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => 'Submit',
    );
    $form['fba-workflow'] = array(
      '#type' => 'hidden',
      '#value' => 'delete',
    );
    $form['fba-page'] = array(
      '#type' => 'hidden',
      '#value' => 2,
    );

    return $form;
  }

  /**
   * Create page three.
   *
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   *
   * @return array
   */
  public function createPageThree($pid, $client, array &$form_state) {
    // Set up the batch array for ingest and update batch processing.

    $batch = array(
      'title' => t('Processing ...'),
      'file' => drupal_get_path('module', 'islandora_content_model_forms') . '/DeleteObjectFilesForm.inc',
      'operations' => array(), // Operations to complete, in order. Defined below.
      'finished' => 'completeDeleteObjectDatastreamsProcessing',
      'init_message' => t('Initialising...'),
      'progress_message' => t('Processed @current out of @total.'),
      'error_message' => t('An error was encountered while processing links.'),
    );

    $datastreamstodelete = array_filter(explode('|', $form_state['values']['dstodelete']));
    foreach ($datastreamstodelete as $datastreamtodelete) {
      $batch['operations'][] = array('removeRelsintAndCols', array($pid, $datastreamtodelete));
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

    $form['fba-workflow'] = array(
      '#type' => 'hidden',
      '#value' => 'delete',
    );
    $form['fba-page'] = array(
      '#type' => 'hidden',
      '#value' => 3,
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
    switch ($form_state['storage']['step']) {
      case 1: {
          break;
        }
      case 2: { //show which csvs have been selected and ask if ok
          break;
        }
      case 3: {
          $form_state['storage'] = NULL;
          $form_state['redirect'] = "viewer/{$pid}/overview";
          break;
        }
      default: {
          break;
        }
    }
  }

}

// Batch functions used in the object datastream functionality

function removeRelsintAndCols($pid, $datastreamtodelete, &$context) {
  $object = islandora_object_load($pid);

  //Remove the RELS-INT links and the cols datastream
  $objectnumber = substr($datastreamtodelete, 3);
  foreach ($object as $datastream) {
    $dsid = $datastream->id;
    $dsidprefix = substr($dsid, 0, strpos($dsid, '-'));

    if (strcmp($dsidprefix, 'COLS' . $objectnumber) === 0) {
      $dsids[] = $dsid;
    }
  }

  $objsrelations = $object['OBJ' . $objectnumber]->relationships;
  foreach ($dsids as $relsintdsid) {
    if ($objsrelations->get('info:fedora/fedora-system:def/relations-internal#', 'hasConstituent', $pid . '/' . $relsintdsid, RELS_TYPE_URI)) {
      $objremovalresult[$relsintdsid] = $objsrelations->remove('info:fedora/fedora-system:def/relations-internal#', 'hasConstituent', $pid . '/' . $relsintdsid, RELS_TYPE_URI);
    }
    $objpurgeresult[$relsintdsid] = $object->purgeDatastream($relsintdsid);
  }
  //Delete the actual datastream
  $dspurgeresult[$datastreamtodelete] = $object->purgeDatastream($datastreamtodelete);
  $context['results']['objremovalresult'] = $objremovalresult;
  $context['results']['objpurgeresult'] = $objpurgeresult;
  $context['results']['dspurgeresult'] = $dspurgeresult;
  $context['results']['pid'] = $pid;
}

/**
 * The final function called by the drupal batch processing
 * functionality.
 * @return
 *   None
 */
function completeDeleteObjectDatastreamsProcessing($success, $results, $operations) {
  if ($success) {
    $msg = '';
    $pid = $results['pid'];
    $purgedds = key($results['dspurgeresult']);
    $purgeddsresult = $results['dspurgeresult'][$purgedds];

    if (!$purgeddsresult) {
      $msg .= 'This datastream wasn\'t purged correctly : ' . $pid . '/' . $purgedds . '</br>';
    }

    foreach ($results['objpurgeresult'] as $objpurgekey => $objpurgeresult) {
      if (!$objpurgeresult) {
        $msg .= 'This object wasn\'t purged correctly : ' . $pid . '/' . $objpurgekey . '</br>';
      }
    }

    foreach ($results['objremovalresult'] as $objremovalkey => $objremovalresult) {
      if (!$objremovalresult) {
        $msg .= 'This object wasn\'t removed correctly : ' . $pid . '/' . $objpurgekey . '</br>';
      }
    }

    if ($msg) {
      drupal_set_message(t('There has been an error in processing, please pass this message on to the Repository Manager (dis @ fba.org.uk) : <br/>' . $msg));
    }
    else {
      drupal_set_message(t('All deletions complete.<br/>'));
    }
  }
  else {
    $msg = 'A linking error occurred while processing the object with the ID ' . $results['currentPid'];
    drupal_set_message(t($msg));
  }
  $context['finished'] = 1;
}

/**
 * Handles the submission of the manage form.
 *
 */
function sidora_batch_delete_form_submit(array $form, array &$form_state) {
  //module_load_include('inc', 'islandora_content_model_forms', 'ManageObjectFilesForm');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');

  $isWizard = $_POST['wizard_check'] === 'true';
  if ($form_state['storage']['step'] == 2) {
    // Ignore normal ingest messages, no idea why this is here, oh well.
    unset($_SESSION['messages']);
    // Needs to be called before $ingest_form->submit() as it
    // removes the $form_state['storage']
    $parent = islandora_object_load($form_state['values']['collection_pid']);
    $_SESSION['pid_list'] = array(); //Wizard will pop pids off of this
    $_SESSION['orig_pid_list'] = array(); //Original set of pids for this action (used for delete / purge on cancel)
    if (!empty($_SESSION['codebook_created'])) {
      array_push($_SESSION['orig_pid_list'], $_SESSION['codebook_created']);
    }
    $_SESSION['codebook_created'] = ''; //Clear out if a codebook has been created for this add resources
    //If the add resources gets cancelled we need to delete a codebook (if it's been created)
    //codebook_created will only be filled in if the codebook was created in the most recent add resources

    if ($isWizard) {
      $_SESSION['current_pid'] = array_pop($_SESSION['pid_list']);
    }
  }
}
