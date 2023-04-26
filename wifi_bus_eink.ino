/**
 *  @filename   :   epd7in5-demo.ino
 *  @brief      :   7.5inch e-paper display demo
 *  @author     :   Yehui from Waveshare
 *
 *  Copyright (C) Waveshare     July 10 2017
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documnetation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to  whom the Software is
 * furished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <SPI.h>
#include <ArduinoLowPower.h>


#include "epd7in5_V2.h"
#include "imagedata.h"
#include "network.h"
#include "bus_description.h"
#include "battery_monitor.h"

struct RenderElement {
  int x;
  int y;

  Element el;

  RenderElement* next = nullptr;
};

RenderElement* render_elements = nullptr;

unsigned char printElements(bool last_in_line, int x_byte, int y) {
  int x = x_byte * 8;
  RenderElement* curr_re = render_elements;
  char intersected_elements = 0;
  while (curr_re != nullptr) {
    if (y >= (curr_re->y - DIGIT_HEIGHT) && y < curr_re->y && x + 7 >= curr_re->x && x < (curr_re->x + curr_re->el.byte_width*8)) {
      // We're in this element!
      int element_left_byte_offset = (y - curr_re->y + DIGIT_HEIGHT) * curr_re->el.byte_width + (x - curr_re->x)/8;
      int element_right_byte_offset = (y - curr_re->y + DIGIT_HEIGHT) * curr_re->el.byte_width + (x + 8 - curr_re->x)/8;
      //    x
      //    v
      //    --want--
      //  |_left_||_right_|
      //  So, want (left << (x%8)) | (right >> (8 - x%8))
      char left_byte = (x >= curr_re->x) ? pgm_read_byte_near(curr_re->el.data + element_left_byte_offset) : 0;
      char right_byte = (((x - curr_re->x)/8 + 1) < curr_re->el.byte_width)? pgm_read_byte_near(curr_re->el.data + element_right_byte_offset) : 0;
      intersected_elements = intersected_elements | ((left_byte << ((x - curr_re->x + 8)%8)) | (right_byte >> (8 - ((x - curr_re->x + 8)%8))));

      // if x is 6 to the left of curr_re->x, i just want right_byte >> 6.
      // x - curr_re->x == -6

      //   x
      //   v
      //     |__el__|
    }
    curr_re = curr_re->next;
  }
  return intersected_elements;
}

void Render(Element* els, int len, int y) {
  int curr_x = 0;
  for (int i = 0; i < len; i++) {
    RenderElement* el = new RenderElement();
    el->x = curr_x;
    el->y = y;
    el->el = {byte_width: els[i].byte_width, advance: els[i].advance, data : els[i].data};
    el->next = nullptr;
    curr_x += el->el.advance;
    if (!render_elements) {
      render_elements = el;
    } else {
      RenderElement* tail = render_elements;
      while(tail->next) {
        tail = tail->next;
      }
      tail->next = el;
    }
  }
}

void ClearRenderElements() {
  RenderElement* next;
  while(render_elements) {
    next = render_elements->next;
    delete render_elements;
    render_elements = next;
  }
}

void RenderBusDescs(BusDescription* descs, int count, int status) {
  ClearRenderElements();
  Serial.print("RenderBusDescs with count ");
  Serial.println(count);
  for (int i = 0; i < count; i++) {
    Element line[20];
    int el_idx = 0;
    for (int j = 0; j < 3; j++) {
      line[el_idx++] = DIGITS[descs[i].number[j] - '0'];
    }
    line[el_idx++] = SEP;

    for (int j = 0; j < 5; j++) {
      if (descs[i].time[j] == ':') {
        line[el_idx++] = COLON;
      } else {
        line[el_idx++] = DIGITS[descs[i].time[j] - '0'];
      }
    }
    line[el_idx++] = SEP;

    line[el_idx++] = DIGITS[(descs[i].stop_id / 1000) % 10];
    line[el_idx++] = DIGITS[(descs[i].stop_id / 100) % 10];
    line[el_idx++] = DIGITS[(descs[i].stop_id / 10) % 10];
    line[el_idx++] = DIGITS[(descs[i].stop_id) % 10];

    Render(line, el_idx, (i+1) * DIGIT_HEIGHT);
  }

  int battery_percentage = readBatteryPercent();
  int status_els = 8;
  Element status_line[9];
  status_line[0] = STATUS;
  status_line[1] = DIGITS[(status / 10) % 10];
  status_line[2] = DIGITS[status % 10];
  status_line[3] = SEP;
  status_line[4] = BATT;
  if (battery_percentage >= 100) {
    status_els = 9;
    status_line[5] = DIGITS[battery_percentage / 100];
    status_line[6] = DIGITS[(battery_percentage / 10) % 10];
    status_line[7] = DIGITS[battery_percentage % 10];
    status_line[8] = PERCENT;
  } else {
    status_line[5] = DIGITS[(battery_percentage / 10) % 10];
    status_line[6] = DIGITS[battery_percentage % 10];
    status_line[7] = PERCENT;
  }
  Render(status_line, status_els, 480);
}

Epd epd;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  Serial.println("Setup");
  batterySetup();
}

void refreshDisplay() {
  connectWifi();
  BusResults results = queryWebService();
  endWifi();
  
  Serial.print("Result: ");
  Serial.println(results.result);
  if (results.result != 0 ) {
    RenderBusDescs(nullptr, 0, results.result * -1);
    return;
  }


  Serial.print("Result count: ");
  Serial.println(results.len);
  for (int i = 0; i < results.len; i++) {
    char buffer[9];
    memcpy(buffer, results.descs[i].number, 3);
    buffer[3] = '\0';
    Serial.print(buffer);
    Serial.print(" - ");
    memcpy(buffer, results.descs[i].time, 5);
    buffer[5] = '\0';
    Serial.print(buffer);
    Serial.print(" ");
    Serial.println(results.descs[i].stop_id);
  }

  if (epd.Init() != 0) {
      Serial.print("e-Paper init failed\r\n ");
      return;
  }

  RenderBusDescs(results.descs, results.len, 0);

  epd.DisplayBytes(&printElements);
  epd.Sleep();
}

void loop() {
  refreshDisplay();

  Serial.println("Done, sleeping");
  // Sleep for 10s, staying away if we're not connected to a serial connection over USB for debugging/programming
  const int kRefreshPeriodMillis = 5 * 60 * 1000;
  if (Serial) {
    delay(kRefreshPeriodMillis);
  } else {
    LowPower.deepSleep(kRefreshPeriodMillis);
  }
}
