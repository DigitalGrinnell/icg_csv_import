<?php

/**
 *  @file 
 * Helper functions for loading and parsing CSV data
 */

/**
 * function icg_csv_load_csv_data
 * @param resource $csvfile the CSV file to be parsed
 * @param char $delimiter the delimiter between columns (set by default to be comma)
 * @return the contents of the file in an array 
*/

function icg_csv_load_csv_data($csvfile, $delimiter=",") { 
  $fh = fopen($csvfile, "r");
  $contentsA = array();

  while (($data = fgetcsv($fh,0,$delimiter)) !== FALSE) {
     array_push($contentsA, $data);
  }
  fclose($fh);
  return $contentsA;
} // end FUNCTION icg_csv_load_csv_data


/**
 * function icg_csv_remove_special_chars
 * Callback function to remove &'"<> for XML ingest
 * @param array $val value to be parsed
*/

function icg_csv_remove_special_chars(&$val) {
	$val = htmlspecialchars($val, ENT_QUOTES);
}

/**
 * function icg_csv_parse_csv_data
 * @param array $csvA array of data (most likely coming from icg_csv_load_csv_data)
 * @param char $delimiter the delimiter between fields in one cell (set by default to be pipe)
 * @return an array where first value is array of headers, rest of values are arrays of rows 
*/
function icg_csv_parse_csv_data($csvA,$delimiter="|") {
// make sure that we escape XML special characters: <>&'"
array_walk_recursive($csvA, 'icg_csv_remove_special_chars');

//ASSUMPTION: first row in array is headers
  $headers = 0;
  $headersA = array();
  $contentsA = array();

  foreach($csvA as $csv) {
      if ($headers == 0) {
	$headersA = $csv;
	$headers++;	
      }
      else {
	foreach($csv as &$delim_row) {
		if (stristr($delim_row, $delimiter)) { // hard coded for now
			$delim_row = explode($delimiter, $delim_row);
		} // end IF $delim_row has a pipe
	} // end FOREACH csv
        array_push($contentsA, $csv); // $row_data);
      }
  } // end WHILE csv is open

  // now, we have two arrays: one with the first line, and the other with the rest of the delimited data
  /*
	[0] => array(heading stuff)
	[1] => array(data)
	
	WANT: array([0] => array(headers);
		    [1] => array(row1);
		    [2] => array(row2);
		etc.
		)
   */
	array_unshift($contentsA, $headersA);
	return $contentsA;
} // end FUNCTION icg_csv_parse_csv_data
?>
