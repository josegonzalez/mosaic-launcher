#ifndef RACHD_H
#define RACHD_H

// rcheevos' CD reader with CHD images read through libchdr, the way
// minarch's reader gives rcheevos their sectors, so a CHD hashes to what
// minarch works out for it. Anything that isn't a .chd goes to rcheevos'
// own reader (cue, ccd, toc, iso and bin).

#include <rc_hash.h>

// The reader, for an rc_hash_iterator's callbacks.cdreader.
void RAChd_reader(rc_hash_cdreader_t* out);

#endif
