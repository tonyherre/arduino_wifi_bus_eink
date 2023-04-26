#ifndef bus_description_h
#define bus_description_h

struct BusDescription {
  char number[3];
  char time[5];
};

struct BusResults {
  int result;
  BusDescription* descs;
  int len;
};

#endif