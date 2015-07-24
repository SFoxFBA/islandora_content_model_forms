<?php



function tagobjectforcloning($pid, $selectedcollection, $licencepid, $isrootelement, &$context) {
  $fedora_object = islandora_object_load($pid);
  $dtca = $fedora_object['DTCA'];
  $dtcacontent = $dtca->content;
  $dtcads = simplexml_load_string($dtcacontent);

  //$dtcads->archivalPublicationVersion++;

  $nowdate = sprintf("%sT%s.000Z", date("Y-m-d"), date("H:i:s"));
  $dtcads->archivalPublicationDate = $nowdate;
  if ($isrootelement) {
    $dtcads->archivalPublicationState = 'submittedtop';
    //so that the repomgr process knows the main activity submitted
  }
  else {
    $dtcads->archivalPublicationState = 'submitted';
  }
  $dtcads->archivalPublicationLicencePid = $licencepid;
  $dtcads->archivalPublicationCollection = $selectedcollection;
  $fedora_object['DTCA']->content = $dtcads->asXML();
}