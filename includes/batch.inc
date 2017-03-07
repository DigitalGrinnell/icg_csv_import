<?php

/*
 * Cells with NO data (empty cells) are generally NOT processed.  However, this fouls
 * up things in the case of enumerated xpaths since they MUST be processed in proper
 * numeric order!
 */

/**
 * Read the CSV into a set of batch operations and fire them off.
 *
 * Orignially lifted from
 * https://github.com/GiantRobot/icg_csv_import/blob/7.x-1.x/icg_csv_import.module
 *
 */
function icg_csv_import_build_batch( ) {
  $module_name = basename(__FILE__, '.module');
  static $update;

  $batch = array(
    'title' => t('Importing CSV ...'),
    'operations' => array(),
    'init_message' => t('Commencing'),
    'progress_message' => t('Processed @current out of @total.'),
    'error_message' => t('An error occurred during processing'),
    'finished' => 'icg_csv_import_import_finished',
  );

  if (($csv_filename = variable_get('icg_csv_import_csv_file_uri', 'Undefined!')) === 'Undefined!') {
    drupal_set_message("icg_csv_import_csv_file_uri must specify a valid CSV import file path!  This process is aborted!", 'error');
    return;
  }

  $delimiter = variable_get('icg_csv_import_delimiter', 'tab');
  $delimiter = (($delimiter === 'tab') ? "\t" : $delimiter);

  if (($parent_pid = variable_get('icg_csv_import_parent_pid', 'None')) === 'None') {
    drupal_set_message("icg_csv_import_csv_parent_pid must specify a valid parent collection PID!  This process is aborted!", 'error');
    return;
  }

  // OK, open the CSV file and Go!
  if ($handle = fopen($csv_filename, 'r')) {
    $headers = fgetcsv($handle, 0, $delimiter);   // first line is $headers

    $format = 'Y-m-d H:i:s';
    $marker = date($format);

    $cData = array( );

    // If $constants are defined, append their keys to $headers.
    $constants = variable_get('icg_csv_import_constants_file_uri', 'None');
    $constants = (($constants === 'None') ? FALSE : $constants);
    if (!empty($constants)) {
      if ($file_handle = fopen($constants, 'r')) {
        $cHeaders = fgetcsv($file_handle, 0, $delimiter);  // Read first line to get xpath/header info
        $cData = fgetcsv($file_handle, 0, $delimiter);     // Read the next line to get $cData
        for ($i = 0; $i < count($cHeaders); $i++) {
          $headers[] = $cHeaders[$i];
        }
        fclose($file_handle);  // Close the constants file connection
      }
    }

    // Check the $headers for errors.
    $result = icg_csv_import_header_errors($headers);
    if (!$result) {
      drupal_set_message("An error was detected by icg_csv_import_header_errors and the import was aborted.", 'error');
      return FALSE;
    } else if (is_array($result)) {
      foreach ($result as $cell => $message) {
        drupal_set_message("An Xpath error was detected in '$cell'.  Message: $message", 'error');

      }
      return FALSE;
    } else {
      $mods_skeleton = $result;  // $mods_skeleton is a valid MODS XML string.
    }

    // Parse the $csv_filename into constituent parts.
    $info = pathinfo($csv_filename);
    $extension = (empty($info['extension']) ? 'csv' : $info['extension']);

    // Open a history file and write the $marker cell followed by $headers.
    $history = 'public://CSV_import_history_'.$marker.'.'.$extension;
    if (!$temp = fopen($history, 'w')) {
      watchdog($module_name, "Could not create CSV history file '%history'.", array('%history' => $history), WATCHDOG_ERROR);
      return FALSE;
    }

    if (!$fp = fopen($history, 'w')) {
      watchdog($module_name, "Could not open CSV history file '%history'.", array('%history' => $history), WATCHDOG_ERROR);
      return FALSE;
    }

    $record = $headers;

    // Determine if a date/time marker is already present in the first $headers cell.  If yes,
    // this process will update the first column, otherwise it will append a new column of data
    // to the history.

    if ($previous = strtotime($record[0])) {
      $update = TRUE;
      $record[0] = $marker;
    } else {
      $update = FALSE;
      array_unshift($record, $marker);
    }

    // Write the $headers to the history file and close it.
    fputcsv($fp, $record, $delimiter);
    fclose($fp);

    // First batch operation...save key parameters
    $controls['history'] = $history;
    $controls['update'] = $update;
    $batch['operations'][] = array('icg_csv_import_remember', array($csv_filename, $controls, $headers));

    // Read and process the CSV file one line at a time
    while ($line = fgetcsv($handle, 0, $delimiter)) {
      // If $constants is defined, append the $cData values to $line
      $full_line = $line;
      if (!empty($cData)) {
        foreach ($cData as $value) {
          $full_line[] = $value;
        }
      }

      // use base64_encode to ensure we don't overload the batch processor by stuffing complex objects into it
      $batch['operations'][] = array('icg_csv_import_import_line', array(array_map('base64_encode', $full_line)));
    }
    fclose($handle);
  }

  // Return path
  $path = '/islandora/object/'.$parent_pid;

  // Set for batch processing...and Go!
  batch_set($batch);
  batch_process($path);
}

/**
 * Handle batch completion.
 *
 * Lifted from https://github.com/GiantRobot/icg_csv_import/blob/7.x-1.x/icg_csv_import.module
 */
function icg_csv_import_import_finished($success, $results, $operations) {
  if ( !empty($results['failed_rows']) ) {
    $dir = 'public://icg_csv_import' ;
    if (file_prepare_directory( $dir, FILE_CREATE_DIRECTORY ) ) {
      $csv_filename = 'failed_rows-'. basename($results['uploaded_filename']); // we validated extension on upload
      $csv_filepath = $dir .'/'. $csv_filename;
      $targs = array(
        '!csv_url' => l(check_plain($csv_filename), file_create_url($csv_filepath)),
        '%csv_filename' => $csv_filename,
        '%csv_filepath' => $csv_filepath,
      ) ;
      if ( $handle = fopen($csv_filepath, 'w+') ) {
        foreach( $results['failed_rows'] as $failed_row ) {
          fputcsv($handle, $failed_row);
        }
        fclose($handle);
        drupal_set_message(t('Some rows failed to import. You may download a CSV of these rows: !csv_url', $targs), 'error');
      }
      else {
        drupal_set_message(t('Some rows failed to import, but unable to write error CSV to %csv_filepath', $targs), 'error');
      }
    }
    else {
      drupal_set_message(t('Some rows failed to import, but unable to create directory for error CSV at %csv_directory', array('%csv_directory' => $dir)), 'error');
    }
  }

  // Print a Drupal message with a link to download the newly generated $history temporary file.
  $rows = $results['rows_imported'];
  $history = $results['parameters']['history'];

  drupal_set_message("CSV import is complete with $rows rows processed.", 'status');
  $targs = array( '!csv_url' => l(check_plain($history), file_create_url($history)), );
  drupal_set_message(t('Click the link provided to download a CSV file reflecting the history of this import: !csv_url', $targs), 'status');

  return t('The CSV import has completed.');
}

/**
 * Remember the uploaded CSV filename, control parameters, and headers.
 *
 * Lifted from https://github.com/GiantRobot/icg_csv_import/blob/7.x-1.x/icg_csv_import.module
 *
 */
function icg_csv_import_remember($filename, $parameters, $headers, &$context) {
  $context['results']['uploaded_filename'] = $filename;
  $context['results']['parameters'] = $parameters;
  $context['results']['headers'] = $headers;
}

/**
 * Process a single line.
 *
 * Lifted from https://github.com/GiantRobot/icg_csv_import/blob/7.x-1.x/icg_csv_import.module
 */
function icg_csv_import_import_line($line, &$context) {
  if (isset($context['results']['rows_imported'])) {
    $context['results']['rows_imported']++;
  } else {
    $context['results']['rows_imported'] = 1;
  }
  $line = $cleaned_line = array_map('base64_decode', $line);

  // dpm($context);

  // Show the import row count
  $context['message'] = t('Importing row !c', array( '!c' => $context['results']['rows_imported'] ));

  // Pull parameters from $context...
  $headers = $context['results']['headers'];

  // Process this line of CSV data.  If the import fails, save the $line in the failed_rows array.
  if (!$pid = icg_csv_import_batch_item($headers, $line, $context['results']['parameters'])) {
    $context['results']['failed_rows'][] = $line;
  }
}

/**
 * icg_csv_import_batch_item - Generate, or update, one object in Islandora from CSV data.
 *
 * Generates one new object, or updates an existing object, in the Islandora
 * repository based on the XPaths ($headers) of the destination metadata fields and corresponding $row data.
 *
 * @param array() $head - XPaths of destination fields
 * @param array() $row - One CSV row of corresponding data to deliver to destination fields
 * @param array() $controls - Process controls passed from above.
 *
 *   history - Name of the temporary file holding processed import history
 *   update - Indicates if this process is updating a previous import (has history) or if objects
 *     are all new
 *
 * @return string|bool - The PID of the ingested object, or FALSE in case of an error
 */
function icg_csv_import_batch_item($head, $row, $controls) {
  $module_name = basename(__FILE__, '.module');
  static $temporary = '/tmp';
  static $ns = 'mods';
  static $nsU = 'http://www.loc.gov/mods/v3';

  $parent_pid = variable_get('icg_csv_import_parent_pid');
  $namespace = variable_get('icg_csv_import_namespace', 'islandora');
  $inactive = variable_get('icg_csv_import_inactive', TRUE);
  $constants = variable_get('icg_csv_import_constants_file_uri', '');
  $delimiter = (variable_get('icg_csv_import_delimiter', 'tab') === 'tab' ? "\t" : variable_get('icg_csv_import_delimiter'));
  $labelXPath = variable_get('icg_csv_import_label_xpath', '/mods/titleInfo/title');
  $credentials['username'] = variable_get('icg_csv_import_username', '');
  $credentials['password'] = variable_get('icg_csv_import_password', '');

  $history = $controls['history'];
  $update = $controls['update'];

  $format = 'Y-m-d H:i:s';
  $marker = date($format);
  $path = drupal_get_path('module','icg_csv_import');

  $record = $row;
  $headers = $head;

  // Check for a hashtag (comment) marker at the start of $row.  If found then echo
  // $row into the history file but do NOTHING else.
  if (_startsWith($record[0],'#')) {
    $fp = fopen($history,'a');            // open the history CSV file to append data
    fputcsv($fp, $record, $delimiter);    // append $record as-is to the history
    fclose($fp);
    return TRUE;
  }

  // Invoke hook_validate_csv_data functions.  If more than one module implements this
  // then skip the ICG_Hooks implementation.
  //
  // Pass the array of $headers (xpaths) and $record (data).  The hook function should return FALSE if there
  // are no problems; otherwise it should return an associative array of
  // content -> message pairs indicating the source of the problem (content) and
  // a message explaining what's wrong.
  //
  // ANYTHING wrong here will cause this line of data to be skipped!

  $skip = FALSE;
  $problems = array( );

  $hooks = module_implements('validate_csv_data');
  if (count($hooks) > 1 && in_array('icg_hooks', $hooks)) { unset($hooks['icg_hooks']); }
  foreach ($hooks as $hook) { $problems[] = module_invoke($hook, 'validate_csv_data', $headers, $record); }
  // $problems = module_invoke_all('validate_csv_data', $headers, $record);

  foreach ($problems as $problem => $message) {
    if ($problem) {
      drupal_set_message("CSV data error encountered in '$problem'.  Description: $message ", 'warning');
      $skip = TRUE;
    }
  }

  // Echo the data into the history file and return FALSE to skip this data.
  if ($skip) {
    $fp = fopen($history,'a');            // open the history CSV file to append data
    fputcsv($fp, $record, $delimiter);    // append $record as-is to the history
    fclose($fp);
    return FALSE;
  }

  // Build a simple MODS DOMDocument to hold the data.
  $doc = new DOMDocument('1.0');
  $doc->preserveWhiteSpace = false;
  $doc->formatOutput = true;
  $mods = $doc->createElementNS($nsU, $ns);
  $mods->setAttributeNS('http://www.w3.org/2000/xmlns/', 'xmlns:xlink', 'http://www.w3.org/1999/xlink');
  $mods->setAttributeNS('http://www.w3.org/2000/xmlns/', "xmlns:$ns", $nsU);
  $mods->setAttributeNS('http://www.w3.org/2000/xmlns/', "xmlns:xsi", 'http://www.w3.org/2001/XMLSchema-instance');
  $doc->appendChild($mods);

  $length = min(count($headers), count($row));
  $obj = $cmodel = $label = NULL;

  // Primary loop...where the rubber meets the road.
  //
  // Loop $i through $headers and $record adding data elements to $doc along the way.
  // Skip all comment (# as first character) rows and empty $headers or $row cells.
  // Look for the optional "special" $headers 'LABEL, 'OBJ_PREFIX', 'OBJ' and 'CMODEL' which mark the
  // columns of $row that contain the object label, OBJ content filepath, and
  // content model, respectively.

  // In case of an $update, shift the $first column one to the right.
  $first = ($update ? 1 : 0);

  for ($i=$first, $parent=NULL; $i<$length; $i++) {
    $data = trim($row[$i]);
    $header = trim($headers[$i]);
    if (empty($header) || _startsWith($header, '#')) { continue; }   // $headers[$i] is blank or a comment...skip this column
    if (empty($data)) { // $data is empty...skip it but ONLY IF $headers[$i] has NO index!
      if (_has_index($header)) {
        $data = "Unknown";  // $headers[$i] carries an index...substitute 'Unknown' in place of $data
      } else {
        continue;
      }
    }
    if ($header === "LABEL") {
      $label = $data;
    } else if ($header === "OBJ_PREFIX") {
      $obj1 = $data;
    } else if ($header === "OBJ") {
      $obj2 = $data;
    } else if ($header === "CMODEL") {
      $cmodel = $data;
    } else if ($header === "MIME") {
      $mime = $data;
    } else {
      if ($header === $labelXPath) { $label = $data; }

      // Check for '|' value seperators in the data.  Break the data into multiple values when found.
      if (!strstr($data,'|')) {
        $parent = _generateNodeFromXPath($doc, $parent, $header, $data);
      } else {
        $data_parts = explode('|', $data);
        $parent = _generateNodeFromXPath($doc, $parent, $header, $data_parts);
      }
    }
  }

  // Concatenate $obj1 and $obj2, if present, to make a full page specification in $obj.
  $obj = '';
  if (!empty($obj1)) { $obj = $obj1; }
  if (!empty($obj2)) { $obj .= $obj2; }

  // Save the completed $doc XML as $mods.
  $mods = $doc->saveXML();

  /* Save $doc as a temporary file for debugging purposes.
  $temp = drupal_tempnam($temporary, 'XML_');
  if (!file_put_contents($temp, $mods)) {
    watchdog($module_name, "Could not save XML as a temporary text file.", array( ) , WATCHDOG_WARNING);
  } else {
    debug("Complete XML saved as '$temp'.");
  } */

  // If we are in $update mode and this record already has a valid PID...purge the existing
  // object's MODS record and force the creation of a new MODS on the same PID ($usePID).

  $pattern = "/^".$namespace.":\d+$/";
  $existing = $update;

  if ($update) {
    $match = preg_match($pattern, $row[0]);
    if ($match == 1) {
      $pid = $row[0];
      if ($object = islandora_object_load($pid)) {
        // $repo = $object->repository;
        // $repo->purgeObject($pid);
        $object->purgeDatastream("MODS");
        drupal_set_message("The MODS record of object '$pid' has been purged and is to be replaced.", 'status');
        $existing = $pid;
      } else {
        drupal_set_message("Object '$pid' could NOT be loaded for update/replacement.", 'error');
        return FALSE;
      }
    } else if (!empty($row[0])) {
      drupal_set_message("Non-comment value ($row[0]) found in CSV data-column[0] is NOT a valid PID. It will be skipped.", 'warning');
      return FALSE;
    } else {
      $existing = FALSE;
    }
  }

  // Create an $object, or $update an $existing object, with $mods as it's MODS datastream
  // and return the object's $pid.

  if ($pid = _icg_make_new_object($existing, $parent_pid, $namespace, $cmodel, $label, $mods, $mime, $obj, $credentials, $inactive)) {
    if ($update) {
      $record[0] = "# ".$pid;
    } else {
      array_unshift($record, "# ".$pid);
    }
  } else {
    if ($update) {
      $record[0] = "# Import Failed";
    }
    else {
      array_unshift($record, '# Import Failed');
    }
  }

  // Append this $row result to the history
  $fp = fopen($history, 'a');    // open the history CSV file to append data
  fputcsv($fp, $record, $delimiter);         // append the $pid and data ($row)
  fclose($fp);

  return $pid;
}


/**
 * icg_csv_import_header_errors - This is derived from icg_csv_import_batch_item as
 *   a function to validate Xpaths (CSV and constants header row) before the
 *   batch process is started.
 *
 * @param array() $head - XPaths from the structured CSV header row.
 *
 * @return string|bool|array - An associative array of $head => message errors, FALSE
 * if the function failed to build a MODS skeleton, or a valid MODS skeleton string.
 */
function icg_csv_import_header_errors($head) {
  $module_name = basename(__FILE__, '.module');
  static $ns = 'mods';
  static $nsU = 'http://www.loc.gov/mods/v3';
  $return = FALSE;

  // Is $head[0] a timestamp?
  $update = strtotime($head[0]);

  // Fetch the remaining controls.
  $labelXPath = variable_get('icg_csv_import_label_xpath', '/mods/titleInfo/title');

  $headers = $head;

  // Invoke hook_validate_csv_header functions.  If hooks other than ICG_Hooks are defined, skip ICG_Hooks.
  //
  // Pass the array of $headers (xpaths).  The hook function should return FALSE if there
  // are no problems; otherwise it should return an associative array of
  // content -> message pairs indicating the source of the problem (content) and
  // a message explaining what's wrong.
  //
  // ANYTHING wrong here should halt the process!

  $halt = FALSE;
  $problems = array( );

  $hooks = module_implements('validate_csv_header');
  if (count($hooks) > 1 && in_array('icg_hooks', $hooks)) { unset($hooks['icg_hooks']); }
  foreach ($hooks as $hook) { $problems[] = module_invoke($hook, 'validate_csv_header', $headers); }
  // $problems = module_invoke_all('validate_csv_header', $headers);

  foreach ($problems as $problem => $message) {
    if ($problem) {
      drupal_set_message("CSV header error encountered in '$problem'.  Description: $message ", 'error', FALSE);
      $halt = TRUE;
    }
  }

  if ($halt) {
    $msg = "Critical CSV header error encountered!  Aborting this process.";
    drupal_set_message($msg, 'error', FALSE);
    return FALSE;
  }

  // Build a simple MODS DOMDocument to test the Xpath structures.
  $doc = new DOMDocument('1.0');
  $doc->preserveWhiteSpace = false;
  $doc->formatOutput = true;

  $xmlns = $doc->createAttribute('xmlns');
  $xmlns->value = $nsU;
  $root = _make_node($doc, $doc, $ns, $xmlns, NULL);

  $length = count($headers);
  $cmodel = $label = NULL;

  // Primary loop...where the rubber meets the road.
  //
  // Loop $i through $headers adding empty data elements to $doc along the way.
  // Skip all empty or comment $headers cells.
  // Look for the optional "special" $headers 'LABEL, 'OBJ_PREFIX', 'OBJ' and 'CMODEL' which mark the
  // columns that contain the object label, OBJ content filepath, and content model, respectively.

  // In case of an $update, shift the $first column one to the right.
  $first = ($update ? 1 : 0);

  for ($i=$first, $parent=NULL; $i<$length; $i++) {
    $header = trim($headers[$i]);
    if (empty($header) || _startsWith($header, '#')) { continue; }   // $headers[$i] is blank or a comment...skip this column

    if ($header === "LABEL") {
      $label = TRUE;
    } else if ($header === "OBJ_PREFIX") {
      $obj1 = TRUE;
    } else if ($header === "OBJ") {
      $obj2 = TRUE;
    } else if ($header === "CMODEL") {
      $cmodel = TRUE;
    } else if ($header === $labelXPath) {
      $label = TRUE;
    }

    $parent = _generateNodeFromXPath($doc, $parent, $header, NULL);
  }

  if ($mods = $doc->saveXML()) {
    return $mods;
  } else {
    return FALSE;
  }
}


// ------------------------ Private functions ----------------------------

/**
 * If the MODS xpaths are already in the CSV file and XPath mapping is skipped...
 * call any/all registered hook_fetch_CSV_defaults functions.
 *
 * @return array $return
 *   'label_field' => Specifies the MODS field used to define an object's LABEL.  Usually /mods/titleInfo/title.
 *   'transform' => Specifies the full path of the MODS-to-DC transform to run on each object MODS record.
 *
 */
function _fetch_CSV_defaults( ) {
  // Call all modules implementing 'hook_fetch_CSV_defaults':
  $return = module_invoke_all('fetch_CSV_defaults');
  return $return;
}


/**
 * Call any/all registered hook_fetch_OBJ functions.  If a hook function other
 * than ICG_Hooks is defined, skip ICG_Hooks.
 *
 * Note: This function returns ONLY ONE fetch_OBJ call!
 *
 * @param string $path - Path of the content (file) to fetch.
 * @param array $credentials
 *   'username' => The username to use for successful file transfer.
 *   'password' => The corresponding password for file transfer.
 */
function _fetch_OBJ($path, $credentials) {
  // Call all modules implementing 'hook_fetch_OBJ':

  $hooks = module_implements('fetch_OBJ');
  if (count($hooks) > 1 && in_array('icg_hooks', $hooks)) {
    unset($hooks['icg_hooks']);
  }
  foreach ($hooks as $hook) {
    $return = module_invoke($hook, 'fetch_OBJ', $path, $credentials);
    //  $return = module_invoke_all('fetch_OBJ', $path, $credentials);
    return $return;
  }
}

/**
 * Generate a new XML node from an XPath specification.
 * @TODO...MODS is assumed to be the target schema.
 *
 * @param $doc
 * @param $parent
 * @param $xpath
 * @param null $data
 * @param string $previous
 */
function _generateNodeFromXPath($doc, $parent, $xpath, $data=NULL, $previous='' ) {
  static $docRoot = '/mods:mods';
  static $ns = 'mods';
  static $nsU = 'http://www.loc.gov/mods/v3';

  $attributes = array( );

  // If no parent is provided, assume that we begin from the document root
  $root = $doc->documentElement;
  $parent = (!$parent ? $root : $parent );

  // Trim the $xpath and grab the next element name in the $xpath
  $trimmed = trim($xpath, '/');
  $parts = _multiexplode(array('/','[',']'), $trimmed);

  // Pop the $element off of the $parts array
  $element = array_shift($parts);

  $predicate = $attr = FALSE;

  // Look ahead for a node $predicate in $parts.  If none is present assume $predicate = 1.
  if (isset($parts[0]) && _is_unsigned_integer($parts[0])) {    // element has an index
    $predicate = array_shift($parts);
  } else {
    $predicate = 1;
  }

  // Look ahead for an $attribute in $parts
  if (isset($parts[0]) && _startsWith($parts[0], '@')) {   // element has an attribute
    $attr = trim($parts[0],"@] ");
    array_shift($parts);
  }

  // If there are one or more $attributes, build their XML.
  if ($attr) {
    $attrArray = _parseAttributes($attr);
    $attrXpath = FALSE;

    foreach ($attrArray as $name => $value) {
      // Found attribute with no value while processing data... skip this attribute.
      if ($data && empty($value)) { continue; }

      try {
        if (!$attributes[$name] = $doc->createAttribute($name)) {
          $msg = "createAttribute($name) in '$xpath' returned FALSE.  Check your Xpath!";
          throw new Exception($msg);
        }
        $attributes[$name]->value = $value;

        // Note that attributes in the header ($data=NULL) should be represented by [@name]
        // with NO value.

        if ($attrXpath) {
          $attrXpath .= " and @$name";
        } else {
          $attrXpath = "@$name";
        }
        if ($data && !empty($value)) { $attrXpath .= "='$value'"; }

      } catch (Exception $e) { // catch all exceptions
        drupal_set_message("Attribute error in '$name' while parsing '$xpath'. Message: "
          . $e->getMessage(), 'error');
        return;
      }
    }
  }

  // Append $element, $predicate and $attr to $previous to make a new $query xpath
  if ($element) { $query = $previous.'/'.$ns.':'.$element; }
  if ($predicate) { $query .= "[$predicate]"; }
  if ($attr) { $query .= "[$attrXpath]"; }

  // If this is not the end of the $xpath, don't set $data!
  if (isset($parts[0])) {
    $text = NULL;
  } else if (is_array($data)) {
    $text = $data;
  } else {
    $text = trim($data," \t\n\r\0\x0B\"");
  }

  // Search for an existing $query node and make it the $parent.
  // If none exist, create a new node.
  // If $query is $docRoot select the document root as the $parent.

  if ($query === $docRoot) {
    $parent = $root;
  } else {
    $dx = new DOMXPath($doc);
    $dx->registerNamespace($ns, $nsU);
    $nodes = $dx->query($query);

    // If $query[$predicate] is found, set $parent to the $predicate node and move on.
    // If $query not found, append new child to $parent and make it the $parent.
    // If multiple $data values exist...make a node from the first, then copy it.

    if ($nodes && ($nodes->length > 0)) {
      $parent = $nodes->item(0);
    } else if (!is_array($text)) {
      $parent = _make_node($doc, $parent, $element, $attributes, $text);
    } else {
      $original_parent = $parent;
      $parent = _make_node($doc, $parent, $element, $attributes, $text[0]);
      $n = count($text);
      for ($i=1; $i<$n; $i++) {
        _make_node($doc, $original_parent, $element, $attributes, $text[$i]);
      }
    }
  }

  // If there are any $parts left, build the $remainder xpath,
  // pass $query as the last xpath processed, and recurse.

  if (isset($parts[0])) {
    $remainder = implode('/',$parts);
    _generateNodeFromXPath($doc, $parent, $remainder, $data, $query);
  }
}

/**
 * Make a new Fedora object with specified characteristics and belonging to an existing
 * parent object (presumably a 'collection').
 *
 * @TODO... MODS is assumed here!
 *
 * @param boolean $existing - When TRUE, this function will replace the $existing
 *   object's MODS record.  Otherwise a new object will be created.
 * @param $parent_pid - The PID of the new object's parent.  Presumably this is a collection object.
 * @param $ns - The namespace (no trailing colon) of the new object.
 * @param string $cmodel - The content model to be associated with the new object.
 * @param string $label - The label (title) to be applied to the Fedora object.
 * @param string $mods - The MODS record content as a string.
 * @param string $mime - The specified MIME type of the object.
 * @param string $obj - The name of the file holding the OBJ datastream content.
 * @param array $credentials
 *   'username' => The username to use for successful file transfer.
 *   'password' => The corresponding password for file transfer.
 * @param bool $inactive - TRUE if the object is to be made inactive.
 *
 * @return mixed - The PID of the new object, or FALSE if the process failed.
 */
function _icg_make_new_object($existing, $parent_pid, $ns, $cmodel, $label, $mods, $mime='image/jpeg', $obj=NULL, $credentials=NULL, $inactive=FALSE) {
  $module_name = basename(__FILE__, '.module');

  $content_model = (!$cmodel ? 'islandora:compoundCModel' : $cmodel);
  $collection_object = islandora_object_load($parent_pid);

  if ($collection_object) {
    $repo = $collection_object->repository;
  }
  else {
    watchdog($module_name, "Could not open parent object '%parent'.", array('%parent' => $parent_pid), WATCHDOG_ERROR);
    return FALSE;
  }

  // If this is NOT an update of an $existing object...
  if (!$existing) {
    $object = $repo->constructObject($ns);    // construct an object in the specified $ns namespace
  } else if (!$object = islandora_object_load($existing)) {
    watchdog($module_name, "Could not open existing object '%pid' to be updated.", array('%pid' => $existing), WATCHDOG_ERROR);
    return FALSE;
  }

  $object->models = $content_model;         // associate it with $content_model
  $object->label = $label;                  // use the label provided

  // Make a new MODS datastream for $object from $doc.
  $dsid = 'MODS';
  $datastream = $object->constructDatastream($dsid, 'X');
  $datastream->label = 'MODS Record';
  $datastream->mimeType = 'text/xml';
  $datastream->setContentFromString($mods);
  $object->ingestDatastream($datastream);

  // Set the object's isMemberOfCollection relation so it becomes part of
  // the $parent_pid collection.
  if (!$existing) {
    $object->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $parent_pid);
  }

  // If $obj is specified, fetch the file contents and make a new OBJ datastream for
  // $object from it.
  if ($obj) {
    if ($temp = _fetch_OBJ($obj, $credentials)) {
      $dsid = 'OBJ';
      $datastream = $object->constructDatastream($dsid, 'M');
      $datastream->label = 'Object Content';
      $datastream->mimeType = $mime;
      $datastream->setContentFromFile($temp, TRUE);
      $object->ingestDatastream($datastream);
    }
  }

  // Run the specified MODS-to-DC transform, if any.
  $modsToDC = variable_get('icg_csv_import_mods-to-dc', drupal_get_path('module', 'icg_csv_import').'/mods-to-dc.xsl');
  if (!$transform = fopen($modsToDC, 'r')) {
    drupal_set_message("Could not open MODS-to-DC transform file '$modsToDC'.", 'error', FALSE);
    return FALSE;
  } else {
    fclose($transform);
  }

  $xml = new DOMDocument;
  if (!$xml->loadXML($mods)) {
    drupal_set_message("Could not load $object->id MODS for transform to DC.", 'error');
    return FALSE;
  }

  $xsl = new DOMDocument;
  if (!$xsl->load($modsToDC)) {
    drupal_set_message("Could not load '$modsToDC' as XSLT for MODS-to-DC transform.", 'error', FALSE);
    return FALSE;
  }

  $xslt = new XSLTProcessor;
  $xslt->importStyleSheet($xsl);   // attach the xsl rules
  $dc = $xslt->transformToXML($xml);     // do it

  $dsid = 'DC';
  $datastream = $object->constructDatastream($dsid, 'X');
  $datastream->label = 'Dublin Core Record';
  $datastream->mimeType = 'text/xml';
  $datastream->setContentFromString($dc);
  $object->ingestDatastream($datastream);   // save transform product as DC datastream

  // Ingest the new object into Fedora.
  $pid = $object->id;
  if (!$existing) { islandora_add_object($object); }

  // If the $label was blank, issue a warning.
  if (strlen($label) < 1) {
    $term = (!$existing ? 'created' : 'updated');
    drupal_set_message("Object '$pid' was $term with NO label.", 'warning');
    $msg = "You may need to introduce a LABEL column in your CSV data, or define the icg_csv_import_label_xpath variable prior to import.";
    drupal_set_message($msg, 'status', FALSE);
  }

  // Do some derivative work if needed.
  if ($obj && $temp) { islandora_run_derivatives($object, 'OBJ'); }

  /* This would be a good place to force a FGSearch/Solr index update, but how?
  if ($fedora = variable_get('icg_csv_import_fedora_repo', FALSE)) {
    $url = $fedora . 'gsearch/rest/rest?operation=updateIndex&action=fromPid&value=' . urlencode($pid);
    $result = curl_query( $url );
  } */

  // Perform any object post-creation cleanup.
  module_invoke_all('create_object_post_ops', $pid);

  // Now, if the object is to be "inactive", set it so.
  if ($inactive) {
    $api_m = $object->repository->api->m;
    $owner = $object->owner;
    $timestamp = $api_m->modifyObject($pid, "I", $owner, "Object set inactive via CSV import.");
  }

  $pidURL = $GLOBALS['base_url'].'/islandora/object/'.$pid;
  $targs = array( '!obj_url' => l(check_plain($pid), check_plain($pidURL)), );
  drupal_set_message(t("Successfully created new object '!obj_url' via CSV import.", $targs), 'status');

  return $pid;
}

/**
 * Make derivatives.
 *
 * @param AbstractObject $object - The ingested object in need of derivatives.
 *
function _make_derivatives($object) {
$module_name = basename(__FILE__, '.module');
$cmodels = $object->models;
foreach ($cmodels as $cmodel) {
switch ($cmodel) {
case 'islandora:sp_basic_image':
module_load_include('inc', 'islandora_basic_image', 'includes/derivatives');
islandora_basic_image_create_thumbnail($object);
islandora_basic_image_create_medium_size($object);
return TRUE;
break;
default:
watchdog($module_name, "Derivatives cannot be created for object '%pid' with CModel type '%cmodel'.", array(
'%pid' => $object->id,
'%cmodel' => $cmodel
), WATCHDOG_WARNING);
return FALSE;
break;
}
}
}

/**
 * Make a new XML node to represent $element with an optional $value.
 *
 * @param $doc
 * @param $parent
 * @param $element
 * @param $attributes - Array of name->value attribute pairs.
 * @param null $value
 * @return DOMElement - Returns the new XML element.
 */
function _make_node($doc, $parent, $element, $attributes, $value=NULL) {
  static $ns = 'mods';
  static $nsU = 'http://www.loc.gov/mods/v3';

  $localAttr = $attributes;

  // Create our target element ($node) with the $ns namespace.

  try {
    $node = $doc->createElementNS($nsU, $element);   // create new $node named $element
  } catch (Exception $e) {
    echo "Caught createElement exception in _make_node: ".$e->getMessage()."\n";
    echo "Element name is '".$element."' and value is '".$value."'\n";
    exit("Unrecoverable exception encountered in _make_node.createElement( ).");
  }

  // If this element has text/data ($value), parse it for possible [@attribute=something]
  // and attach the text to the element.

  if ($value) {
    if (strpos(" $value",'[@')) {
      list($data, $attr) = explode("[@", $value);
      if ($attr) {
        $pairs = _parseAttributes($attr, FALSE);    // append data attributes to header $attributes
        foreach ($pairs as $name => $val) {
          try {
            if (!$localAttr[$name] = $doc->createAttribute($name)) {
              $msg = "createAttribute($name) in data '$value' returned FALSE.  Check your Xpath!";
              throw new Exception($msg);
            }
            $localAttr[$name]->value = $val;
          } catch (Exception $e) { // catch all exceptions
            drupal_set_message("Attribute error in '$name' while parsing data '$value'. Message: "
              . $e->getMessage(), 'error');
            return;
          }
        }
      }
    } else {
      $data = $value;
    }

    if (!$text = $doc->createTextNode(trim($data))) {   // save $data as new $text node
      drupal_set_message("createTextNode($value) returned FALSE.  Check your data!", 'error');
      return FALSE;
    }

    try {
      $node->appendChild($text);   // attach the $value to the new $node
    } catch (Exception $e) { // catch all exceptions
      drupal_set_message("appendChild(text) exception.  Message: " . $e->getMessage(), 'error');
      return FALSE;
    }
  }

  // If any $localAttr attributes are defined, append them to our $node.
  // @TODO...probably should check to make sure no attribute is defined more than once!

  if ($localAttr) {
    foreach ($localAttr as $attribute) {
      try {
        $node->appendChild($attribute);   // attach the $attribute to the new $node
      } catch (Exception $e) { // catch all exceptions
        drupal_set_message("appendChild(attribute) exception.  Message: " . $e->getMessage(), 'error');
        return FALSE;
      }
    }
  }

  try {
    $node = $parent->appendChild($node);  // attach the new $node to $parent and return
  } catch (Exception $e) { // catch all exceptions
    drupal_set_message("appendChild(node) exception.  Message: " . $e->getMessage(), 'error');
    return FALSE;
  }

  return $node;
}

/**
 * Explode a string using multiple delimiters.  This code modified from
 * http://php.net/manual/en/function.explode.php
 *
 * @param $delimiters
 * @param $string
 * @return array
 */
function _multiexplode($delimiters, $string) {
  $ready = str_replace($delimiters, $delimiters[0], $string);
  $parts = explode($delimiters[0], $ready);
  $stack = array( );
  foreach($parts as $part) {
    $p = trim($part," /[]");
    if (strlen($p)>0) {
      $stack[] = $p;
    }
  }
  return $stack;
}

/**
 * Determine if a string starts with a particular character.  This code lifted
 * from http://stackoverflow.com/questions/834303/startswith-and-endswith-functions-in-php
 *
 * @param $haystack
 * @param $needle
 * @return bool
 */
function _startsWith($haystack, $needle) {
  // search backwards starting from haystack length characters from the end
  return $needle === "" || strrpos($haystack, $needle, -strlen($haystack)) !== false;
}

/**
 * Determine if a $val represents an unsigned, whole integer.  Lifted from
 * http://php.net/manual/en/function.is-numeric.php
 *
 * @param $val
 * @return boolean
 */
function _is_unsigned_integer($val) {
  $val=str_replace(" ", "", trim($val));
  $test = preg_match("/^([0-9])+$/", $val);
  if ($test == 1) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
 * Determine if $txt has a '[n]' index where n is any integer.
 *
 * @param $txt
 * @return bool
 */
function _has_index($txt) {
  $pattern = "/\w*\[\d+].*/";
  if (preg_match($pattern, $txt) === 1) { return TRUE; }
  return FALSE;
}

/**
 * Parse a [bracketed] single or multiple attribute clause of the form
 * general form [@single='a'] or [@one='a' and @two='b' and @three='c'].
 *
 * Note that when parsing attributes in the header it is permissible to omit
 * the equal sign (=) and value for any/all attributes.  For example, this
 * syntax is valid in the header:  [@one='a' and @two and @three='c'].
 * However, attributes in the data MUST conform to the @name='value' syntax.
 *
 * @param $attr string - A [bracketed] attribute spec.
 * @return array - Attribute name->value pairs.
 */
function _parseAttributes($attr, $header=TRUE) {
  $attributes = array();
  $attrs = explode(' and ', $attr);

  foreach ($attrs as $a) {
    $pair = trim($a, "@] ");
    if ($header && !strpos($pair,'=')) {
      $name = $pair;
      $val = NULL;
    } else {
      list($name, $val) = explode('=', $pair);
    }

    $attributes[$name] = trim(trim($val,"'"),'"');
  }
  return $attributes;
}
