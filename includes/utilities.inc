<?php
/**
 * Utilities for ICG CSV Batch.
 * br2490
 */

/**
 * @param IslandoraFedoraObject $collection
 *
 * @return array
 *   An array describing each model allowed in this collection
 *   - pid: The PID with which the content model object.
 *     - pid: The PID with which the content model object.
 *     - name: The default label to use for new child objects of this type.
 *     - namespace: The default namespace for new child objects of this type.
 *
 * @throws Exception
 */
function _icg_csv_import_get_available_ingest_models(
  IslandoraFedoraObject $collection) {

  module_load_include('inc', 'islandora', 'includes/utilities');
  module_load_include('inc', 'xml_form_builder', 'includes/associations');

  $all_content_models = islandora_get_content_models();

  if (!($policy = $collection['COLLECTION_POLICY'])) {
    throw new Exception('No collection policy was found for this collection.
     Please contact your site admin.');
  }

  $policy = new CollectionPolicy($policy->content);
  $policy_content_models = $policy->getContentModels();

  $ret = array();
  foreach ($policy_content_models as $available_model) {

    // We'll use the pid as the key for our array.
    $pid = $available_model['pid'];

    // Build our return array, keyed by $pid.

    // This provides the human readable name of the cModel.
    $ret['models'][$pid] = $all_content_models[$pid]['label'];

    // This provides the namespace.
    $ret['namespaces'][$pid] = $available_model['namespace'];

    // Grab all associated forms, regardless of DSID.
    $associations = xml_form_builder_get_associations(
      array(), $available_model, array(), FALSE);

    // Build an array of forms, though I think we're only using MODS (?).
    foreach ($associations as $association) {
      $ret['forms'][$pid][] = $association;
    }
  }

  return $ret;
}

/**
 * Small callback for ajax to get associated forms.
 *
 * @param $selected_cmodel
 * @param $xml_forms
 *
 * @return array
 */
function _icg_ajax_callback_xml_form($selected_cmodel, $xml_forms) {
  $ret = array();

  $cmodel_forms = $xml_forms[$selected_cmodel];

  foreach ($cmodel_forms as $form_name) {
    // Do a check here "['enabled']" should be TRUE.
    $name = $form_name['form_name'];
    $ret[$name] = $name;
  }

  return $ret;
}

/**
 * Leaving this here in case it needs to be a SPARQL query.
 *
 * STUB - silly function but here in case this needs to be more robust. Right
 * now we're relying completely on what the COLLECTION_POLICY dictates about
 * namespace; Either we'll need to traverse *->RELS_EXT to find out
 * about all the namespaces... or just SPARQL bright.
 *
 * @param $selected_cmodel
 * @param $namespaces
 *
 * @return array
 */
function _icg_ajax_callback_ns_form($selected_cmodel, $namespaces) {
  $ret = array();

  $ns = $namespaces[$selected_cmodel];
  $ret[$ns] = $ns;

  return $ret;
}

/**
 * Iterates through a form array and replaces specific fields and their options.
 *
 * @param $form - The metadata form as an object.
 * @param array $array - The metadata form as an array.
 * @param array $mappable_options - The vector of CSV data to map.
 * @param string $form_name - The metadata form name (or NULL if called from within).
 *
 * @return array - Corresponding XPaths.  @TODO
 */
function _icg_form_array_iterator_set_options(&$form, &$array, $mappable_options, $form_name=NULL) {
  static $xpaths = array( );
  static $position;

  static $manipulable_fields = array(
    'textfield',
    'textarea',
    'tags',
    'select',
  );

  // On the first call to this function $form_name should be provided, so use it
  // to fetch all possible xpaths. 
  if (isset($form_name)) {
    $xpaths = _icg_csv_import_get_xpaths($form_name, $array);
  }

  // If this branch of the form is a manipulable field...
  if (isset($array['#type']) && in_array($array['#type'], $manipulable_fields)) {
    $oldType = $array['#type'];
    $title = $array['#title'];
    $array['#type'] = 'select';
    if ($oldType != 'select') { $array['#options'] = $mappable_options; }
    
    $hash = $array['#hash'];
    if (!$found = _icg_csv_import_recursive_array_search($xpaths, '#hash', $hash)) {
      drupal_set_message("Could not find #hash => '$hash' for $oldType form element '$title' in the XPaths array.", 'Error');
    } else if (!isset($found[0]['xpath'])) {
      drupal_set_message("The $oldType form element '$title' has no XPath.", 'Warning');
    } else {
      $xp = $found[0]['xpath'];
      $array['#description'] = 'This '. $oldType .' field will map to <b>' . $xp . '</b>.';
    }
  }
  
  // This branch of the form (tree) is not a manipulable field, break it down further
  else if (is_array($array)) {
    foreach($array as $key => &$value){
      if (isset($value) && is_array($value)) {
        _icg_form_array_iterator_set_options($form, $value, $mappable_options);
      }
    }
  }

  return $xpaths;
}


/**
 * This is a modified copy of function xml_form_builder_get_title_options($form_name)
 *
 * @param $form_name
 * @param $form
 * @return array
 */
function _icg_csv_import_get_xpaths($form_name, $form) {

  // Traverse through the the $hashes copy of $form and find all the element #hash values.
  $hashes = $form;
  _icg_csv_import_gather_hashes($hashes);
  _icg_csv_import_convert_array_keys($hashes, '#type', '#originalType');
  _icg_csv_import_convert_array_keys($hashes, '0', 'x');
  _icg_csv_import_remove_numeric_keys($hashes);

  // Now load and process the $form, using $form_name, to build an array of corresponding xpaths.
  if (isset($form_name)) {
    module_load_include('inc', 'xml_form_builder', 'XMLFormRepository');
    $doc = XMLFormRepository::Get($form_name);  // This is a DOMDocument representation of the form

    if ($doc) {
      $debugXML = $doc->saveXML($doc);
      $xpath = new DOMXPath($doc);
      $result = $xpath->query('//element[@name]');
      // $result = $xpath->query('//element[@name] | //@*');   // failed attempt to get attributes
      $elements = dom_node_list_to_array($result);   // a mix of DOMElement and DOMAttr nodes
      if (count($elements) > 0) {
        $output = array( );

        foreach ($elements as $element) {
          $debugXML = $element->ownerDocument->saveXML($element);
          $test = $xpath->evaluate(
            'ancestor-or-self::node()[@name]/properties/type[text()="textfield" or text()="select" or text()="textarea"]', $element);
          if ($test->length === 0) { continue; }
          $result = $xpath->query('ancestor-or-self::node()[@name]', $element);
          $ancestors = dom_node_list_to_array($result);
          $ancestors_array = array();

          if (count($ancestors) > 0) {
            foreach ($ancestors as $ancestor) {
              $name = $ancestor->getAttribute('name');
              // Does this $ancestor have an enumerator?
              $ancestors_array[] = (string) $name;
            }
          }

          $x = "/mods/" . implode('/', $ancestors_array);
          $val = str_replace('/0', '[x]', $x);
          $branch = _icg_csv_import_build_branch($ancestors_array, $val);
          $output = array_merge_recursive($output, $branch);
        }
        $output = array_merge_recursive($output, $hashes);
        _icg_csv_import_convert_array_keys($output, '0', 'xpath');
        _icg_csv_import_remove_numeric_keys($output);

        return $output;
      }
    }
  }
  return array(NULL => t('Metadata Form Not Found!'));
}

/**
 * @param array $a - Ordered array of ancestor elements.
 * @param string $xp - Corresponding xpath.
 */
function _icg_csv_import_build_branch($a, $xp) {
  $n = count($a);

  switch ($n) {
    case 5:
      $branch[$a[0]] = array($a[1] => array($a[2] => array($a[3] => array($a[4] => $xp))));
      break;
    case 4:
      $branch[$a[0]] = array($a[1] => array($a[2] => array($a[3] => $xp)));
      break;
    case 3:
      $branch[$a[0]] = array($a[1] => array($a[2] => $xp));
      break;
    case 2:
      $branch[$a[0]] = array($a[1] => $xp);
      break;
    case 1:
      $branch[$a[0]] = $xp;
      break;
  }

  _icg_csv_import_convert_array_keys($branch, '0', 'x');
  _icg_csv_import_remove_numeric_keys($branch);

  return $branch;
}

/**
 * Remove all but the #hash and #type key/value pairs from $array.
 *
 * @param array $array - The $form, or branch of the $form, as an array.
 */
function _icg_csv_import_gather_hashes(&$array) {
  static $toss = array('#actions', '#options');
  static $keep = array('#hash', '#type');
  foreach ($array as $key => &$value) {
    if (in_array($key, $toss, TRUE)) {   // toss out unneeded arrays
      unset($array[$key]);
    } else if (is_array($value)) {  // traverse into all remaining arrays
      _icg_csv_import_gather_hashes($value);
    } else if (!in_array($key, $keep)) {  // found a key/value pair and the key is not #hash or #type, toss it
      unset($array[$key]);
    }
  }
  return;
}

/**
 * @param $array
 * @param $find
 * @param $replace
 */
function _icg_csv_import_convert_array_keys(&$array, $find, $replace) {
  foreach ($array as $key => &$value) {
    // Change $find to $replace
    if ($key == $find) {
      $array[$replace] = $value;
    }

    // Recurse into the tree branch
    if (is_array($value)) {
      _icg_csv_import_convert_array_keys($value, $find, $replace);
    }
  }

  return;
}

/**
 * @param $array
 */
function _icg_csv_import_remove_numeric_keys(&$array) {
  foreach ($array as $key => &$value) {
    // Change $find to $replace
    if (is_numeric($key)) {
      unset($array[$key]);
    }
    else {
      // Recurse into the tree branch
      if (is_array($value)) {
        _icg_csv_import_remove_numeric_keys($value);
      }
    }
  }

  return;
}

/**
 * @param $array
 * @param $key
 * @param $value
 * @return array
 */
function _icg_csv_import_recursive_array_search($array, $key, $value)
{
  $results = array();

  if (is_array($array))
  {
    if (isset($array[$key]) && $array[$key] == $value)
      $results[] = $array;

    foreach ($array as $subarray)
      $results = array_merge($results, _icg_csv_import_recursive_array_search($subarray, $key, $value));
  }

  return $results;
}

