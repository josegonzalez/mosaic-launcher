#ifndef OPENMODEL_H
#define OPENMODEL_H

// The SDL-free half of plugins' entry_open: which entries plugins are
// asked about, and which plugin opens one itself.

// 1 when plugins are asked about opening an entry of this type (an enum
// MosaicEntryType): a game or a pak, which would otherwise launch.
int OpenModel_asks(int type);

// Asks plugins 0 to count - 1 in turn, through ask, until one says it
// opened the entry itself (ask returns 1). Returns that plugin's index, or
// -1 when none did; anything but 1 counts as no.
int OpenModel_first(int count, int (*ask)(int index, void* data), void* data);

#endif
