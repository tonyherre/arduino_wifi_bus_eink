#ifndef LOGGING_H
#define LOGGING_H

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(str) Serial.print(str);
#define DEBUG_PRINTLN(str) Serial.println(str);
#else
#define DEBUG_PRINT(str)
#define DEBUG_PRINTLN(str)
#endif

#endif // ifndef LOGGING_H