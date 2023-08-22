#ifndef network_h
#define network_h

#include "bus_description.h"
bool checkWifi();
int connectWifi();
BusResults queryWebService();
int endWifi();

#endif