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
      else {
        $simplecheck[] = $valobjectkey;
      }
    }
    // validate each of the fieldgroups 

    return $validationobjects;
  }

  public function getMandatoryFields($pid) {
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
    //$object_linker_form = $object_linker->getForm();
    //compile the list of form fields that have "fieldvalidator" as an attribute

    $xpath = new DOMXPath($object_linker_definition);
    $results = $xpath->query("//mandatory");
    $rescount = $results->length;

    $mandatoryobjects = array();
    for ($i = 0; $i < $rescount; $i++) {
      //need to get 'name' from topmost element in nodepath
      $node = $results->item($i);
      $nodepath = $node->getNodePath();
      //$name = $node->nodeName;
      //$val = $node->nodeValue;
      //try and find actions/read/path
      //$readpos = strpos($nodepath, 'attributes/fieldvalidator');
      //$readpos = strpos($nodepath, 'properties/attributes/mandatory');
      //$readprefix = substr($nodepath, 0, $readpos);
      //$readpath = $readprefix . '/actions/read/path';
      //$readval = $xpath->evaluate($readpath);
      //$readvalitem = $readval->item(0)->nodeValue;
      //remove the '/dtca:xxxx/' prefix
      //$sxmldtca->registerXPathNamespace('dtca', 'http://fba.org.uk/schemas/dtca/1.0.0');
      //$sxmldtca->registerXPathNamespace('gco', 'http://www.isotc211.org/2005/gco');
      //$xpathresult = $sxmldtca->xpath($readvalitem);
      //$xpathresulttext = (string) $xpathresult[0];

      $pos = strpos($nodepath, 'properties/attributes/mandatory');
      //$prefix = substr($nodepath, 0, $pos);
      //$titlepath = $prefix . '/title';
      //$titleval = $xpath->evaluate($titlepath);
      //$titlevalitem = $titleval->item(0)->nodeValue;
      //$continueloop = TRUE;

      $prefix = substr($nodepath, 0, $pos - 1); //-1 to remove last slash
      $namepath = 'string(' . $prefix . '/@name)';
      $nameval = $xpath->evaluate($namepath);
      //while (is_null($titlevalitem) && $continueloop) {
      //  $pos = strrpos($prefix, 'children/element');
      //  $prefix = substr($nodepath, 0, $pos);
      //  if (strpos($prefix, 'element') === FALSE) {
      //    $continueloop = FALSE;
      //  }
      //  else {
      //    $titlepath = $prefix . '/title';
      //    $titleval = $xpath->evaluate($titlepath);
      //    $titlevalitem = $titleval->item(0)->nodeValue;
      //  }
      // }
      //if (is_null($titlevalitem)) {
      //  $titlevalitem = 'Warning - title field missing';
      //}
      $mandatoryobjects[] = $nameval;
    }
    return $mandatoryobjects;
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
    //module_load_include('inc', 'fedora_repository', 'ObjectHelper');
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    module_load_include('inc', 'content_model_viewer', 'Collection');
    global $user;
    $elements = array();
    //Does this object have any resources?
    $childresources = $this->buildChildrenTree($pid, $elements, $user->islandora_user_pid);

    $hasconcept = FALSE;
    $hasresource = FALSE;
    $conceptns = sidora_return_fba_classes_namespaces();
    $resourcens = sidora_return_fba_resources_namespaces();
    foreach ($childresources as $childresource) {
      if (in_array(substr($childresource, 0, strpos($childresource, ':')), $conceptns)) {
        $hasconcept = TRUE;
      }
      if (in_array(substr($childresource, 0, strpos($childresource, ':')), $resourcens)) {
        $hasresource = TRUE;
      }
    }
    $haschildren = ($hasconcept && $hasresource);

    $fullpidlist = array_merge(array($pid), $childresources); //put the activity at the top, where it should logically appear
    $currentrelstext = '<table>
            <tr class="fbabordertop">
                <td class="fbatype">Type</td>
                <td class="fbatitle">Title</td>
                <td class="fbafields">Missing Mandatory</td>
                <td class="fbakeywordshead" colspan="3" align="center">Missing Keywords</td>
                <td class="fbanuminputs">Number of inputs</td>
                <td class="fbascope">In scope?</td>
            </tr>
            <tr class="fbaborderbottom">
                <td class="fbatype">&nbsp;</td>
                <td class="fbatitle">&nbsp;</td>
                <td class="fbafields">Metadata fields</td>
                <td class="fbakeywordssubject">Subject</td>
                <td class="fbakeywordsgeo">Geographic</td>
                <td class="fbakeywordstaxon">Taxonomic</td>
                <td class="fbanuminputssmall">(Data Components only)</td>
                <td class="fbascope">&nbsp;</td>
            </tr>';
    $inscopeflag = TRUE;
    $hasnoemptyfields = TRUE;
    foreach ($fullpidlist as $objectpid) {
      $isinscope = TRUE;
      $objtype = $this->_getType($objectpid);

      $currentobject = islandora_object_load($objectpid);
      $objlabel = $currentobject->label;

      //***********************
      //Column 3
      //check if the object's metadata has correctly completed all mandatory fields
      //***********************
      $dtcadatastream = $this->getMetadata($objectpid, 'DTCA', $client);
      $domdtca = new DOMDocument();
      $domdtca->loadXML($dtcadatastream);
      //test bit v
      $test = $domdtca->saveXML();
      $sxmldtca2 = simplexml_load_string($dtcadatastream);
      $frv = $this->getFieldsRequiringValidation($objectpid, $sxmldtca2);
      //test bit ^
      $sxmldtca = simplexml_load_string($dtcadatastream);
      $sxmlnamespaces = $sxmldtca->getNamespaces(TRUE); //some fields have namespaces, still need to be able to read them
      foreach ($sxmlnamespaces as $nskey => $ns) {
        $children = $sxmldtca->children($ns);
        $$nskey = $children;
        if ($nskey) {
          $sxmldtca->registerXPathNamespace($nskey, $ns . '/');
        }
        else {
          $sxmldtca->registerXPathNamespace('nskey', $ns . '/');
        }
      }
      $mandatoryfields = $this->getMandatoryFields($objectpid);
      $emptyfieldcount = 0;
      foreach ($mandatoryfields as $mandatoryfield) {
        $nodels = $domdtca->getElementsByTagName($mandatoryfield);
        foreach ($nodels as $node) { //Does this need to be a loop?
          $nodeval = $node->nodeValue;
        }
        if (strlen(trim($nodeval)) === 0) {
          $emptyfieldcount++;
        }
      }
      if ($emptyfieldcount > 0) {
        $hasnoemptyfields = FALSE;
        $emptyfieldcountstring = '<span style="color: red"> * </span>';
      }
      else {
        $emptyfieldcountstring = (string) $emptyfieldcount;
      }
      //End of Column 3
      //***********************
      //
      //Column 4
      //check that all keywords are in the appropriate vocabulary
      $dtcakeywords = $sxmldtca->keyword;
      $numkeywords = $dtcakeywords->count();

      $subjectkeywords = array();
      $geokeywords = array();
      $taxokeywords = array();
      for ($i = 0; $i < $numkeywords; $i++) {
        $keywordattrauthority = (string) $dtcakeywords[$i]->attributes()->authority;
        $dtcakeywordchild = $dtcakeywords[$i]->children('http://www.isotc211.org/2005/gco');
        $dtcacharstring = (string) $dtcakeywordchild->CharacterString;
        if (strlen($dtcacharstring) > 0) {
          switch ($keywordattrauthority) {
            case 'fba subject': {
                $subjectkeywords[] = $dtcacharstring;
                break;
              }
            case 'fba geographic': {
                $geokeywords[] = $dtcacharstring;
                break;
              }
            case 'fba taxonomic': {
                $taxokeywords[] = $dtcacharstring;
                break;
              }

            default: {
                break;
              }
          }
        }
      }
      //Now check if all those keywords are valid
      $validsubjectkeywords = $validgeokeywords = $validtaxokeywords = TRUE;
      foreach ($subjectkeywords as $keywordtofind) {
        $validsubjectkeywords = $this->_sparql_title_search($keywordtofind);
      }
      foreach ($geokeywords as $keywordtofind) {
        $validgeokeywords = $this->_sparql_title_search($keywordtofind);
      }
      foreach ($taxokeywords as $keywordtofind) {
        $validtaxokeywords = $this->_sparql_title_search($keywordtofind);
      }

      $missingsubjectkeywords = ($validsubjectkeywords ? 'No' : '<span style="color: red">Yes</span>');

      $missinggeokeywords = ($validgeokeywords ? 'No' : '<span style="color: red">Yes</span>');

      $missingtaxokeywords = ($validtaxokeywords ? 'No' : '<span style="color: red">Yes</span>');

      // Need to confirm child resources have the correct number of inputs
      // put a function in here to return the number of inputs maybe xpath on <DTCA datastream, dtca:input/<gco:CharacterString, <keyword authority
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

        $numinputmessage = '&nbsp;';
        $pidns = substr($objectpid, 0, strpos($objectpid, ':'));
        switch ($pidns) {
          case 'dtcan': {
              if ($numinputs > 1) {
                $validnumberofinputs[] = TRUE;
                $numinputmessage = $numinputs;
              }
              else {
                $validnumberofinputs[] = FALSE;
                $numinputmessage = '<span style="color: red">' . $numinputs . '</span>';
              }
              break;
            }
          case 'dtcmt': {
              if ($numinputs == 0) {
                $validnumberofinputs[] = TRUE;
                $numinputmessage = $numinputs;
              }
              else {
                $validnumberofinputs[] = FALSE;
                $numinputmessage = '<span style="color: red">' . $numinputs . '</span>';
              }
              break;
            }
          case 'dtclr': {
              if ($numinputs > 0) {
                $validnumberofinputs[] = TRUE;
                $numinputmessage = $numinputs;
              }
              else {
                $validnumberofinputs[] = FALSE;
                $numinputmessage = '<span style="color: red">' . $numinputs . '</span>';
              }
              break;
            }
          case 'dtcsy': {
              if ($numinputs > 2) {
                $validnumberofinputs[] = TRUE;
                $numinputmessage = $numinputs;
              }
              else {
                $validnumberofinputs[] = FALSE;
                $numinputmessage = '<span style="color: red">' . $numinputs . '</span>';
              }
              break;
            }
          case 'dtcsi': {
              if ($numinputs > 0) {
                $validnumberofinputs[] = TRUE;
                $numinputmessage = $numinputs;
              }
              else {
                $validnumberofinputs[] = FALSE;
                $numinputmessage = '<span style="color: red">' . $numinputs . '</span>';
              }
              break;
            }
          default:
            break;
        }
      }
      else {
        //DTCA datastream is missing, so no validation required. For supplementary files
        $validnumberofinputs[] = TRUE;
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

      //Check to see if any of the datacomponents link to something
      //out of scope, eg it's an input to another datacomponent
      $object = islandora_object_load($objectpid);
      $rels = $object->relationships;

      $icios = $rels->get('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'isComponentInputOf');
      $hdesign = $rels->get('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasDesign');
      $hprot = $rels->get('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasProtocol');
      $hdoc = $rels->get('http://fba.org.uk/schemas/fba/fba_rdf/1.0.0/', 'hasDocumentation');
      $scoperelations = array_merge($icios, $hdesign, $hprot, $hdoc);

      if (count($scoperelations) > 0) {
        foreach ($scoperelations as $scoperelation) {
          $scopeobject = $scoperelation[object];
          $scopevalue = $scopeobject[value];
          if (!in_array($scopevalue, $fullpidlist)) {
            $isinscope = FALSE;
            break; //no need to check any more
          }
        }
      }

      //Check if any other objects have a reverse rdf back to this object 
      //(including data components), which would class it has having an out of 
      //scope condition.
      $designrdfs = array('isComponentInputOf', 'hasDesign', 'hasProtocol', 'hasDocumentation');
      $rdfstocheck = array_merge(sidora_return_forward_fba_concept_rdfs(), sidora_return_forward_fba_resource_rdfs(), $designrdfs);
      $rdfns = array_merge(sidora_return_fba_classes_namespaces(), sidora_return_fba_resources_namespaces());

      $subject = '*';
      $object = '<info:fedora/' . $objectpid . '>';
      $spoResults = array();
      foreach ($rdfstocheck as $rdftocheck) {
        if ((strcmp($rdftocheck, 'hasActivity') === 0) && (strcmp($objectpid, $pid) === 0)) {
          //We can ignore parent activities for the top level object
          break;
        }
        if (in_array($rdftocheck, $designrdfs)) {
          $uri = FBA_URI;
        }
        else {
          $uri = RELS_EXT_URI;
        }
        $spoResult = spo_search($subject, '<' . $uri . $rdftocheck . '>', $object);
        if (!is_null($spoResult)) {
          foreach ($spoResult as $sporesultrow) {
            $subjectpid = $sporesultrow[subjectPid];
            $subjectpidns = substr($subjectpid, 0, strpos($subjectpid, ':'));
            if (in_array($subjectpidns, $rdfns)) { //don't need to worry about the fba-user rdf
              if (!in_array($subjectpid, $fullpidlist)) { //check if pid is in the 'in-scope' list
                $spoResults[] = $sporesultrow;
              }
            }
          }
        }
      }

      if (count($spoResults) > 0) {
        $isinscope = FALSE;
      }
      $currentrelstext .= '<tr class="fbaborderbottom">
                <td class="fbatype">' . $objtype . '</td>
                <td class="fbatitle">' . $objlabel . '</td>
                <td class="fbafields">' . $emptyfieldcountstring . '</td>
                <td class="fbakeywordssubject">' . $missingsubjectkeywords . '</td>
                <td class="fbakeywordsgeo">' . $missinggeokeywords . '</td>
                <td class="fbakeywordstaxon">' . $missingtaxokeywords . '</td>
                <td class="fbanuminputs">' . $numinputmessage . '</td>
                <td class="fbascope">' . ($isinscope ? '&nbsp;' : '<span style="color: red"> X </span>') . '</td>
            </tr>';

      if (!$isinscope) {
        $inscopeflag = FALSE; //a flag to say at least one object has been found to be out of scope
      }
    }

    $currentrelstext .= '</table>';
    $remainingrels = $this->list_available_publication_collections($pid);

    if ($haschildren &&
        $hasnoemptyfields &&
        $passedonallinputs &&
        $validsubjectkeywords &&
        $validgeokeywords &&
        $validtaxokeywords &&
        $inscopeflag &&
        (!is_null($remainingrels[0]))) {

      $form = array(
        'currentcollections' => array(
          '#type' => 'markup',
          '#value' => $currentrelstext,
        ),
        'publishcollection' => array(
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
      $currentrelstext .= '<tr class="fbaborderbottom">'
          . '<td colspan="7"><br/>Some help text to go here to help users complete all the missing things above.<br/></td>'
          . '</tr>';
      if (!$inscopeflag) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - An <span style="color: red"> X </span> indicates that the object is linked as an input/protocol/design/document to an object outside the scope of this hierarchy.</td>'
            . '</tr>';
      }
      if (!$haschildren) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - The requisite minimum number of classes and data components has not been reached.</td>'
            . '</tr>';
      }
      if (!$hasnoemptyfields) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - <span style="color: red"> * </span> indicates missing mandatory metadata fields error.</td>'
            . '</tr>';
      }
      if (!$passedonallinputs) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - Not passed on all inputs error.</td>'
            . '</tr>';
      }
      if (!$validsubjectkeywords) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - Invalid Subject Keywords error.</td>'
            . '</tr>';
      }
      if (!$validgeokeywords) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - Invalid Geo Keywords error.</td>'
            . '</tr>';
      }
      if (!$validtaxokeywords) {
        $currentrelstext .= '<tr class="fbaborderbottom">'
            . '<td colspan="7"><br/> - Invalid taxon keywords error.</td>'
            . '</tr>';
      }

      $currentrelstext .= '</table>';

      $form = array(
        'currentcollections' => array(
          '#type' => 'markup',
          '#value' => $currentrelstext,
        ),
      );
    }
    return $form;
  }

  function list_available_publication_collections($pid) {
    global $user;
    $remainingrels = array();
    switch ($user->name) {
      case 'avondtc' : {
          $remainingrels[] = 'dtcavon:collection';
          break;
        }
      case 'edendtc' : {
          $remainingrels[] = 'dtcaeden:collection';
          break;
        }
      case 'wensumdtc' : {
          $remainingrels[] = 'dtcawensum:collection';
          break;
        }
      case 'fbadata' :
      case 'NBywell' :
      case 'SFox' :
      case 'mhaft' :
      case 'GRushworth': {
          $remainingrels[] = 'fbads:collection';
          break;
        }
      default : {
          $remainingrels[] = NULL;
        }
    }
    /* This was how the collections were retrieved previously
      $currentrels = array();
      $objecttopublish = islandora_object_load($pid);
      $relationships = $objecttopublish->relationships->get(RELS_EXT_URI, 'isMemberOfCollection');
      //Is this in an 'fbauserNN:collection'?
      foreach ($relationships as $relationship) {
      $currentrels[$relationship[object][value]] = $relationship[object][value];
      }
      $futurerels = $this->getPublishCollectionsList();
      $remainingrels = array_diff($futurerels, $currentrels); //Don't want to try and re-add it to an existing collection
     */
    return $remainingrels;
  }

  function _get_mandatory_field_and_values(&$element) {
    $mandatoryvalue = $element['#attributes']['mandatory'];
    if (strcmp($mandatoryvalue, '1') === 0) {
      $title = $element['#title'];
      //$value = $element something or other;
      $results[$title] = $value;
    }

    foreach (element_children($element) as $key) {
      $results .= _get_mandatory_field_and_values($element[$key]);
    }

    return $results;
  }

  function _sparql_title_search($title) {
    $validkeywords = TRUE;

    $queryprefix = 'select $subject from <#ri> where {?subject <http://purl.org/dc/elements/1.1/title> "';
    $querysuffix = '" }';
    $query = $queryprefix . $title . $querysuffix;
    $sparqlresult = ObjectHelper::performRiQuery($query, 'sparql');

    if (count($sparqlresult) === 0) {
      $validkeywords = FALSE;
    }
    return $validkeywords;
  }

  /**
   *
   * @param type $pid
   * @return type
   * SFOX, copied from Collection.inc in Islandora Content Model Viewer and added concepts
   */
  private function _getType($pid) {
    module_load_include('inc', 'fedora_repository', 'ObjectHelper');
    $types = array(
      'fba:activity_cmodel' => 'Activity',
      'fba:dataset_cmodel' => 'Dataset',
      'fba:analysis_cmodel' => 'Analysis',
      'fba:measurement_cmodel' => 'Measurement',
      'fba:simulation_cmodel' => 'Simulation',
      'fba:synthesis_cmodel' => 'Synthesis',
      'fba:litreview_cmodel' => 'Literature Review',
      'fba:supp_basic_image_cmodel' => 'Image',
      'fba:supp_large_image_cmodel' => 'Large Image',
      'fba:supp_pdf_cmodel' => 'PDF',
      'fba:supp_text_cmodel' => 'Text',
      'fba:supp_excel_cmodel' => 'Excel',
      'fba:supp_word_cmodel' => 'Word',
    );
    $object_helper = new ObjectHelper();
    $content_models = $object_helper->get_content_models_list($pid);
    foreach ($content_models as $content_model) {
      if (isset($types[$content_model->pid])) {
        return $types[$content_model->pid];
      }
    }
    return 'Unknown';
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

    $licenceprivacytext = 'Text to explain licensing. <br/><br/>';
    $selectedcollection = $form_state['values']['publishcollection'];
    $remainingrels = $this->list_available_publication_collections($pid);
    $selectedcollname = $remainingrels[$selectedcollection];
    $selectedpid = $form_state['values']['pidtopublish'];
    $licenceprivacytext .= $selectedpid . ' has been chosen for publishing to ' . $selectedcollname . '.<br/><br/>';

    $item = new Fedora_Item('fbalicence:1');

    $dc = $item->get_datastream_dissemination('DC');
    $xml = simplexml_load_string($dc);
    $dc = $xml->asXML();
    $document = new DOMDocument();
    $document->loadXML($dc);
    $xpath = new DOMXPath($document);
    $xpath->registerNamespace('dc', 'http://purl.org/dc/elements/1.1/');
    $results = $xpath->query('//dc:rights');
    $node = $results->item(0);
    $licencetext = $node->textContent;


    $licenceprivacytext .= 'The text of this licence is:<br/>' . $licencetext . '<br/><br/>';
    $form = array(
      'currentcollections' => array(
        '#type' => 'markup',
        '#value' => $licenceprivacytext,
      ),
      'pidtopublish' => array(
        '#type' => 'hidden',
        '#value' => $pid,
      ),
      'acceptlicence' => array(
        '#type' => 'checkbox',
        '#title' => t('I agree to the licence'),
      ),
      'selectedcollection' => array(
        '#type' => 'hidden',
        '#value' => $selectedcollection,
      ),
      'submit' => array(
        '#type' => 'submit',
        '#submit' => array('fedora_repository_publish_qdc_form_submit'),
        '#value' => 'Next',
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

      $form = array('licenceprivacy' => array(
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

    $batch = array(
      'title' => t('First batch run'),
      'file' => drupal_get_path('module', 'islandora_content_model_forms') . '/batchfunctions.inc',
      'operations' => array(),
      'finished' => '_finished',
    );

    $selectedcollection = $form_state['values']['selectedcollection'];
    //Find which descendants will need publishing
    $childresources = $this->buildChildrenTree($pid, $elements, $user->islandora_user_pid);

    $clonelist = array_merge(array($pid), $childresources); //put the activity at the top, where it should logically appear
    //enumerate through clone list to create new objects
    $remainingrels = $this->list_available_publication_collections($pid);
    $selectedcollname = $remainingrels[$selectedcollection];

    $selectcollectionns = substr($selectedcollname, 0, strpos($selectedcollname, ':'));
    foreach ($clonelist as $clonepid) {
      array_unshift($batch['operations'], array('cloneobject', array($clonepid, $selectcollectionns)));

      //Check which of these pids are datasets, as they will require DOI's
      //The whole batch should fail if the DOI assignment is unsuccessful.
      $namespace = substr($clonepid, 0, strpos($clonepid, ':'));
      if (strcmp($namespace, 'dtcds') === 0) {
        $pidstomint[] = $clonepid;
      }
    }
    batch_set($batch);
    //SFOX bad hack to force the non graphical version of batch processing
    //SFOX as sidora catches the url redirect which shows the percentage complete bar
    $batch = & batch_get();
    $batch['progressive'] = FALSE;
    //end of bad hack
    batch_process();

    // run a second batch to correct all the RELS-EXTs
    $batchresultsuccess = $_SESSION['my_batch_succeeded']; //check return results
    $batchresult = $_SESSION['my_batch_results']; //check return results
    //Another final batch job is required here to swap all the old and new pids in the new objects
    $batchtidy['operations'][] = array('tidyuprelations', array($batchresult['success'], $selectcollectionns));
    batch_set($batchtidy);
    //SFOX bad hack to force the non graphical version of batch processing
    //SFOX as sidora catches the url redirect which shows the percent complete bar
    $batchtidy = & batch_get();
    $batchtidy['progressive'] = FALSE;
    //end of bad hack
    batch_process();

    // run a third batch to mint a doi for dataset(s)
    foreach ($pidstomint as $i => $mintpid) {
      foreach ($batchresult['success'] as $completedclone) {
        if (strcmp($mintpid, $completedclone[0]) === 0) {
          //create a batch operation to mint a DOI for this dataset
          $pidtouse[] = $completedclone[1];
        }
      }
    }
    foreach ($pidtouse as $i => $mintpid) {
      $batchmint['operations'][] = array('mintdoi', array($mintpid));
    }

    batch_set($batchmint);
    //SFOX bad hack to force the non graphical version of batch processing
    //SFOX as sidora catches the url redirect which shows the percent complete bar
    $batchmint = & batch_get();
    $batchmint['progressive'] = FALSE;
    //end of bad hack
    batch_process();

    $resultstring = '';
    foreach ($batchresult['success'] as $result) {
      $resultstring .= $result[0] . ' -> ' . $result[1] . ' -> ' . $result[2] . '<br/>';
    }
    $redirecturl = $base_root . $base_path . 'fedora/repository/' . $pid;

    $form = array(
      'currentcollections' => array(
        '#type' => 'markup',
        '#value' => 'These are the objects to be cloned/published to ' . $selectedcollection . ': <br/>' . $resultstring,
      ),
      'finish' => array(
        '#type' => 'markup',
        '#value' => '<a href=' . $redirecturl . ' target="_parent">Needs a link here to return to the object screen (the same as if cancel had been clicked)</a>',
      ),
      'pidtopublish' => array(
        '#type' => 'hidden',
        '#value' => $pid,
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

  function getParentConcepts($pid) {
    $repository = tuque_wrapper_get_repository_instance();
    $ri = $repository->ri;

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


    return $parentconcepts;
  }

  function getChildren($pid) {
    //Does this object have any resources?

    $resourcerdfs = array_merge(sidora_return_forward_fba_concept_rdfs(), sidora_return_forward_fba_resource_rdfs());
    $arraycount = count($resourcerdfs);

    $query = 'select ?object where {';
    foreach ($resourcerdfs as $key => $keyvalue) {
      $query .= '{<info:fedora/' . $pid . '> <info:fedora/fedora-system:def/relations-external#' . $keyvalue . '> ?object}';
      if ($key < $arraycount - 1) { //as arraycounts don't start at zero
        $query .= ' UNION ';
      }
    }
    $query .= '}';

    $repository = tuque_wrapper_get_repository_instance();
    $ri = $repository->ri;

    $sparql_query_results = $ri->sparqlQuery($query);
    foreach ($sparql_query_results as $sparql_query_result) {
      $childresources[] = $sparql_query_result[object][value];
    }

    return $childresources;
  }

  static function getPublishCollectionsList() {
    $repository = tuque_wrapper_get_repository_instance();
    $ri = $repository->ri;
    $query = 'select ?subject where {?subject <info:fedora/fedora-system:def/relations-external#isMemberOfCollection> <info:fedora/dtc:collection>}';
    $sparql_query_results = $ri->sparqlQuery($query);
    foreach ($sparql_query_results as $sparql_query_result) {
      if (strpos($sparql_query_result[subject][value], ':root') === FALSE) { //Bodged code to remove roots for now until MH decides final format
        $colllist[] = $sparql_query_result[subject][value];
      }
    }
    return $colllist;
  }

  static function getFBAPublishCollectionsList() {
    $colllist = array('fbads:collection');
    return $colllist;
  }

  //tree construction algorithm
  function buildParentTree($pid, &$elements, $parent_id = 0) {
    $parentlist = $this->getParentConcepts($pid);
    $branch = array();
    foreach ($parentlist as $node) {
      //$nodeparent = $this->getParentConcepts($node);
      if (strcmp($node, $parent_id) === 0) {
        
      }
      else {
        $children = $this->buildParentTree($node, &$elements, $parent_id);
        if ($children) {
          $branch[] = $children[0];
        }
        $branch[] = $node;
      }
    }
    return $branch;
  }

  function buildChildrenTree($pid, &$elements, $child_id = 0) {
    $childlist = $this->getChildren($pid);
    $branch = array();
    if ($childlist) {
      foreach ($childlist as $node) {
        //$nodeparent = $this->getParentConcepts($node);
        if (strcmp($node, $child_id) === 0) {
          
        }
        else {
          $children = $this->buildChildrenTree($node, &$elements, $child_id);
          if ($children) {
            foreach ($children as $child) {
              $branch[] = $child;
            }
          }
          $branch[] = $node;
        }
      }
    }
    return $branch;
  }

}
