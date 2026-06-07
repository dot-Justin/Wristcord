// src/pkjs/lib/paging.js
// Pure row encoding + paging for Pebble AppMessage transport.
// No IO, no globals.
'use strict';

var RS = '\x1E';  // ASCII 30 — record separator
var US = '\x1F';  // ASCII 31 — unit/field separator

/**
 * encodeRows(records) -> string
 *
 * records: array of field-arrays (each field a string or number).
 * Fields are coerced to strings; any RS or US chars inside fields are removed.
 * Fields joined by US; records joined by RS.
 */
function encodeRows(records) {
  if (!records.length) return '';

  var RS_RE = new RegExp(RS, 'g');
  var US_RE = new RegExp(US, 'g');

  var encodedRecords = records.map(function(fields) {
    var encodedFields = fields.map(function(field) {
      return String(field).replace(RS_RE, '').replace(US_RE, '');
    });
    return encodedFields.join(US);
  });

  return encodedRecords.join(RS);
}

/**
 * batches(rowString, maxLen) -> array of chunk strings
 *
 * Splits rowString by RS into individual records, then greedily groups
 * consecutive records into chunks whose total length (re-joined by RS)
 * is <= maxLen.
 *
 * A single record longer than maxLen gets its own chunk (never split).
 * Empty / '' input returns [].
 */
function batches(rowString, maxLen) {
  if (!rowString) return [];

  var records = rowString.split(RS);
  var chunks = [];
  var current = [];
  var currentLen = 0;

  for (var i = 0; i < records.length; i++) {
    var rec = records[i];

    if (current.length === 0) {
      // Starting a new chunk (a single over-long record lands here and gets its own chunk)
      current.push(rec);
      currentLen = rec.length;
    } else if (currentLen + 1 + rec.length <= maxLen) {  // + RS + rec
      current.push(rec);
      currentLen += 1 + rec.length;
    } else {
      // Won't fit — flush current chunk and start a new one with this record
      chunks.push(current.join(RS));
      current = [rec];
      currentLen = rec.length;
    }
  }

  if (current.length > 0) {
    chunks.push(current.join(RS));
  }

  return chunks;
}

module.exports = {
  encodeRows: encodeRows,
  batches: batches,
  RS: RS,
  US: US,
};
