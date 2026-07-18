/****************************************************************************
 * app/home_scense/sensor.h
 * Temperature, humidity and proximity sensor interface.
 ****************************************************************************/

#ifndef HOME_SCENSE_SENSOR_H
#define HOME_SCENSE_SENSOR_H

#include <lvgl/lvgl.h>

/* Sensor update bitmask */
typedef enum {
    SENSOR_UPDATE_TEMPERATURE = 0x01,
    SENSOR_UPDATE_HUMIDITY    = 0x02,
    SENSOR_UPDATE_PROX        = 0x04,
    SENSOR_UPDATE_ALL         = 0x07
} sensor_select_t;

/* Init / cleanup */
int  sensor_init(void);
void sensor_deinit(void);

/* Poll and update. Returns number of sensors updated or negative on error. */
int  sensor_update(sensor_select_t mask, void *ctx);

/* Create a repeating LVGL timer that polls sensors. */
lv_timer_t *sensor_timer_create(void);

#endif /* HOME_SCENSE_SENSOR_H */
