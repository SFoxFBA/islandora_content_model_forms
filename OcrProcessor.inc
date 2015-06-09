<?php

  /**
   * Prior to creating a temporary directory
   * in the data-area appropriate to the current
   * website, attempt to remove it in case
   * an identical one already exsits.
   * The temporary directory will contain the
   * interim files created as part of the OCR
   * processing. The temporary directory will
   * be removed later in the processing.
   * @param type $ingest_file_location
   * @param type $pdfFilePath
   * @param type $escapedPdfFile
   * @param type $escapedPdfDirPath
   * @param type $tmpDir
   * @param type $tmpDirPath
   * @param type $ocrProcessingRequired
   * @param type $ocrMsg
   * @return type
   */
  function preprocess_pdf_file($ingest_file_location, &$pdfFilePath, &$escapedPdfFile, &$escapedPdfDirPath, &$tmpDir, &$tmpDirPath, &$ocrProcessingRequired, &$ocrMsg) {
    global $base_url;
    foreach ($ingest_file_location as $dsid => $file) {
      if (!strcmp($dsid, 'OBJ')) {
        $pdfFilePath = $file;
      }
    }

    $ocrMsg = 'A problem occurred while attempting to OCR the PDF file ' . $pdfFilePath;

    if (strlen($pdfFilePath) == 0) {
      drupal_set_message(t($ocrMsg));
      $ocrProcessingRequired = FALSE;
      return FALSE;
    }

    $startOfSpecificSite = 7;
    $specificSite = substr($base_url, $startOfSpecificSite);
    $specificSite = str_replace("/", ".", $specificSite);

    switch($specificSite) {
      case "beta.freshwaterlife.org":
        $specificSite = "172.16.0.99.data.local";
        break;
      case "mydata.environmentdata.org":
        $specificSite = "172.16.0.99.mydata.environmentdata.local";
        break;
      default:
        $specificSite = $specificSite;
    }

    $pdfDirPath = dirname($pdfFilePath);
    $pdfFile = basename($pdfFilePath);
    $escapedPdfFile = escapeshellarg($pdfFile);
    $escapedPdfFile = substr($escapedPdfFile, 1, strlen($escapedPdfFile)-2);

    $escapedPdfDirPath = escapeshellarg($pdfDirPath);
    $escapedPdfDirPath = substr($escapedPdfDirPath, 1, strlen($escapedPdfDirPath)-2);
    $escapedPdfFilePath = $escapedPdfDirPath . '/' . $escapedPdfFile;

    $ocrMsg = 'A problem occurred while attempting to OCR the PDF file ' . $escapedPdfFile;
    /* Create a temporary directory that contains a randomly
     * generated element. This will ensure that the OCR processing
     * for two users logged in with the same credentials will not
     * conflict.
     */
    $tmpDir = 'extract_ocr_tmp' . '_' . rand(100000, 999999);
    $tmpDirParentDirPath = 'sites/' . $specificSite . '/files';
    $tmpDirPath = $tmpDirParentDirPath . '/' . $tmpDir;
    $errorFlag = 0;
    /*
     * Remove the temporary directory if it already exists.
     */
    if (file_exists($tmpDirPath)) {
      $execCommand = 'rm -rf ' . $tmpDirPath;
      passthru($execCommand, $returnValue); 
      if ($returnValue != 0) {
        $errorFlag = 1;
      }
    }

    if ($errorFlag == 0) {
      /*
       * Create the temporary directory.
       */
      $execCommand = 'mkdir ' . $tmpDirPath;
      passthru($execCommand, $returnValue); 
      if ($returnValue != 0) {
        $errorFlag = 1;
      }
    }

    if ($errorFlag == 0) {
      /*
       * Copy the PDF to the temporary directory.
       */
      $execCommand = 'cp "' . $escapedPdfFilePath . '" ' . $tmpDirPath;
      passthru($execCommand, $returnValue); 
      if ($returnValue != 0) {
        $errorFlag = 1;
      }
    }

    if ($errorFlag == 1)
    {
      $ocrProcessingRequired = FALSE;
      drupal_set_message(t($ocrMsg));
      return FALSE;
    }
  }

  /**
   * Extract text from PDF to create OCR datastream.
   * Strictly speaking, when using the pdftotext
   * utility, Optical Character Recognitition is
   * not involved, but to retain consistency with
   * other uses the datastreams that serve the
   * same purpose it is called 'OCR'.
   * @param type $pid
   * @param type $pdfFilePath
   * @param type $escapedPdfFile
   * @param type $escapedPdfDirPath
   * @param type $tmpDir
   * @param type $tmpDirPath
   * @param type $ocrMsg
   * @return type
   */
  function extractOcrFromPDF($pid, &$pdfFilePath, &$escapedPdfFile, &$escapedPdfDirPath, &$tmpDir, &$tmpDirPath, $ocrMsg) {
    $errorFlag = 0;
    $pdfToTextMode = 1;
    $tesseractMode = 2;
    $processingMode = $tesseractMode;
    $concatOutputFilePath = $tmpDirPath . '/OCR';

    if ($processingMode == $pdfToTextMode) {
      /*
       * Using the processing in this branch is more efficient if the
       * PDFs were converted from a file originally created via a
       * word-processing package such as Word. However, if the PDF
       * was created by scanning an original, this processing will
       * not extract any text.
       */
      $execPath = '/usr/bin/pdftotext ';
      $execCommand = $execPath . '"' . $tmpDirPath . '/' . $escapedPdfFile . '" ' . $concatOutputFilePath;
      passthru($execCommand, $returnValue);
      if ($returnValue == 0) {
        attachOcrDatastream($pid, $tmpDirPath, $concatOutputFilePath, $ocrMsg);
      } else {
        $errorFlag = 1;
      }
    } else if ($processingMode == $tesseractMode) {
      $granularityOfOcr = 150;
      $ocrOutputFile = 'ocrbook';
      /*
       * In the temporary directory create the file to which the
       * the text extracted from each individual tif file will
       * be appended.
       */
      $output = fopen($concatOutputFilePath, "w");
      $content = "";
      fwrite($output, $content);
      fclose($output);
      /*
       * Run pdftoppm which creates one ppm file for each page
       * of the PDF.
       */
      $execCommand = 'pdftoppm -r ' . $granularityOfOcr . ' "' . $tmpDirPath . '/' . $escapedPdfFile . '" ' . $tmpDirPath . '/' .$ocrOutputFile;
      passthru($execCommand, $returnValue); 
      if ($returnValue == 0) {
        $portablePixmapImageFileExt = 'ppm';
        $dir = opendir($tmpDirPath."/.");
        $ppmFiles = array();
        /*
         * Loop through the ppm files adding the names
         * to an array so that they can be sorted.
         */
        while($item = readdir($dir))
        {
          $ext = pathinfo($item, PATHINFO_EXTENSION);
          if (!strcmp($ext, $portablePixmapImageFileExt)) {
            $ppmFiles[] = $item;
          }
        }
        sort($ppmFiles);
        closedir($dir);
        /*
         * Loop through the sorted ppm file names
         * and convert each one to a tif, then run
         * tesseract on the tif in order to extract
         * the text. Append the text to the relevant
         * file with a string to indicate where each
         * page ends.
         */
        $batch = array(
          'title' => t('Extracting text from PDF pages'),
          'operations' => array(),
          'finished' => 'completeBatchProcessing',
          'init_message' => t('Initializing... '),
          'progress_message' => t('Processed @current out of @total.'),
          'error_message' => t('An error was encountered while extracting text.'),
        );

        foreach ($ppmFiles as $ppmFile) {
          $batch['operations'][] = array('processPDFPages', array($pid, $ocrMsg, $concatOutputFilePath, $tmpDirPath, $ppmFile));
        }

        batch_set($batch);
      } else {
        $errorFlag = 1;
      }
    }
    if ($errorFlag) {
      drupal_set_message(t($ocrMsg));
      return FALSE;
    } else {
      return TRUE;
    }
  }


  /**
   * This is the main batch function that converts
   * an individual .ppm file into a .tif and then uses
   * tesseract to extract the text from the .tif file.
   * The text in the resulting .txt file is appended
   * to the output file that contains the text from
   * all the individual files.
   * @param type $pid
   * @param type $ocrMsg
   * @param type $concatOutputFilePath
   * @param type $tmpDirPath
   * @param type $ppmFile
   * @param type $context
   */
  function processPDFPages($pid, $ocrMsg, $concatOutputFilePath, $tmpDirPath, $ppmFile, &$context) {
    $tessdataPath = '/usr/local/share/tessdata';
    if (strcmp(getenv('TESSDATA_PREFIX'), $tessdataPath)) {
      putenv("TESSDATA_PREFIX=$tessdataPath");
    }
    $convBase = pathinfo($ppmFile,PATHINFO_FILENAME);
    $execCommand = 'convert ' . $tmpDirPath . '/' . $ppmFile . ' ' . $tmpDirPath . '/' . $convBase . '.tif';
    passthru($execCommand, $returnValue);
    if ($returnValue == 0) {
      $execCommand = '/usr/local/bin/tesseract ' . $tmpDirPath . '/' . $convBase . '.tif' . ' ' . $tmpDirPath . '/' . $convBase . ' -l eng >/dev/null';
      passthru($execCommand, $returnValue); 
      if ($returnValue == 0) {
        $execCommand = 'cat ' . $tmpDirPath . '/' . $convBase . '.txt >> ' . $concatOutputFilePath;
        passthru($execCommand, $returnValue);
        if ($returnValue == 0) {
          $execCommand = 'echo "[pagebreak]" >> ' . $concatOutputFilePath;
          passthru($execCommand, $returnValue);
        } else {
          $errorFlag = 1;
        }
      } else {
        $errorFlag = 1;
      }
    } else {
      $errorFlag = 1;
    }

    $context['results']['pid'] = $pid;
    $context['results']['concatOutputFilePath'] = $concatOutputFilePath;
    $context['results']['ocrMsg'] = $ocrMsg;
    $context['results']['tmpDirPath'] = $tmpDirPath;

    $execCommand = 'rm -f ' . $tmpDirPath . '/' . $convBase . '.ppm';
    passthru($execCommand, $returnValue);
    if ($returnValue != 0) {
      $errorFlag = 1;
    }

    $execCommand = 'rm -f ' . $tmpDirPath . '/' . $convBase . '.tif';
    passthru($execCommand, $returnValue);
    if ($returnValue != 0) {
      $errorFlag = 1;
    }

    if ($errorFlag == 1) {
      $context['results']['ocrError'] = 1;
    } else {
      $context['results']['ocrError'] = 0;
    }
  }

  /**
   * This is the final batch function. A check
   * is made whether any errors have been
   * encountered during processing. If one or
   * more errors have been encountered a warning
   * message is output. Otherwise the function
   * that adds the OCR datastream to the ingested
   * object is called.
   * @param type $success
   * @param type $results
   * @param type $operations
   */
  function completeBatchProcessing($success, $results, $operations) {

    if ($results['ocrError'] == 1) {
      /*
       * Remove the temporary directory here
       * as it will not now be done in the
       * attachOcrDatastream function.
       */
      $execCommand = 'rm -rf ' . $results['tmpDirPath'];
      passthru($execCommand, $returnValue);
      drupal_set_message($results['ocrMsg']);
    } else {
      attachOcrDatastream($results['pid'],$results['tmpDirPath'], $results['concatOutputFilePath'], $results['ocrMsg']);
    }
    $context['finished'] = 1;
  }

  /**
   * This is function adds the OCR datastream
   * to the ingested object and removes the
   * temporary directory and any files it
   * still contains.
   * @param type $pid
   * @param type $tmpDirPath
   * @param type $concatOutputFilePath
   * @param type $ocrMsg
   */
  function attachOcrDatastream($pid, $tmpDirPath, $concatOutputFilePath, $ocrMsg) {
    module_load_include('inc', 'fedora_repository', 'api/tuque');
    $errorFlag = 0;

    $fedora_object = islandora_object_load($pid);

    if (fedora_object) {
      $ocrDatastream = $fedora_object->constructDatastream('OCR', 'M');
      $ocrDatastream->mimeType = 'text/xml';
      $ocrDatastream->label = 'OCR';
      $ocrDatastream->setContentFromFile($concatOutputFilePath, FALSE);
      $fedora_object->ingestDatastream($ocrDatastream);
      $message = "Text has been extracted from the PDF";
      drupal_set_message($message);
      /*
       * Remove the temporary directory.
       */
      $execCommand = 'rm -rf ' . $tmpDirPath;
      passthru($execCommand, $returnValue);
      if ($returnValue != 0) {
        $errorFlag = 1;
      }
    } else {
      $errorFlag = 1;
    }

    if ($errorFlag == 1) {
      drupal_set_message(t($ocrMsg));
    }
  }
