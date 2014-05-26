/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

#include "cbar.h"

#include <assert.h>
#include <limits.h>


void cbar_init(struct cbar *cbar, const struct cbar_line_config *configs, struct cbar_line *lines)
{
    cbar->configs = configs;
    cbar->lines = lines;

    pthread_mutex_init(&cbar->mutex, NULL);

    for (int id=0; cbar->configs[id].type; id++) {
        struct cbar_line *line = &cbar->lines[id];
        const struct cbar_line_config *config = &cbar->configs[id];

        /* All lines are initially at zero. */
        line->value = 0;

        switch (config->type) {
            case CROSSBAR_INPUT: {
                line->input.input_value = line->value;
            } break;
            case CROSSBAR_EXTERNAL: {
            } break;
            case CROSSBAR_THRESHOLD: {
            } break;
            case CROSSBAR_DEBOUNCE: {
                /* Make the debouncer start counting immediately. */
                line->debounce.value = INT_MIN;
            } break;
            case CROSSBAR_REQUEST: {
            } break;
            case CROSSBAR_CALCULATED: {
            } break;
            case CROSSBAR_MONITOR: {
                /* Make the monitor fire immediately on the initial state. */
                line->monitor.previous = INT_MIN;
            }
            case CROSSBAR_PERIODIC: {
                line->periodic.elapsed = 0;
            } break;
        }
    }

    cbar_recalculate(cbar, 0);
}

void cbar_recalculate(struct cbar *cbar, int delay)
{
    pthread_mutex_lock(&cbar->mutex);

    for (int id=0; cbar->configs[id].type; id++) {
        struct cbar_line *line = &cbar->lines[id];
        const struct cbar_line_config *config = &cbar->configs[id];

        switch (config->type) {
            case CROSSBAR_INPUT: {
                line->value = line->input.input_value;
            } break;
            case CROSSBAR_EXTERNAL: {
                int input = config->external.get(config->external.priv);
                line->value = config->external.invert ? !input : input;
            } break;
            case CROSSBAR_THRESHOLD: {
                int input = cbar->lines[config->debounce.input].value;

                if (line->value)
                    line->value = (input > config->threshold.threshold_down);
                else
                    line->value = (input >= config->threshold.threshold_up);
            } break;
            case CROSSBAR_DEBOUNCE: {
                int input = cbar->lines[config->debounce.input].value;

                if (input != line->debounce.value) {
                    // Line state just changed. Reset debounce timer.
                    printf("cbar: [debounce] %s going towards %d\r\n", config->name, input);
                    line->debounce.value = input;
                    line->debounce.timer = 0;
                } else if (input != line->value) {
                    // Line state is stabilizing. Bump debounce timer.
                    int timeout = input ? config->debounce.timeout_up : config->debounce.timeout_down;
                    //printf("cbar: [debounce] %s clocked %d vs %d\r\n", config->name, line->debounce.timer, timeout);
                    if ((line->debounce.timer += delay) > timeout) {
                        // Line is stable. Register the change.
                        printf("cbar: [debounce] %s stable at %d\r\n", config->name, input);
                        line->value = input;
                    }
                }
            } break;
            case CROSSBAR_REQUEST: {
            } break;
            case CROSSBAR_CALCULATED: {
                line->value = config->calculated.get(cbar);
            } break;
            case CROSSBAR_MONITOR: {
                int input = cbar->lines[config->monitor.input].value;
                if (input != line->monitor.previous) {
                    printf("cbar: [monitor] %s changed to %d\r\n", config->name, input);
                    line->value = true;
                    line->monitor.previous = input;
                }
            } break;
            case CROSSBAR_PERIODIC: {
                line->periodic.elapsed += delay;
                if (line->periodic.elapsed >= config->periodic.period) {
                    line->periodic.elapsed = 0;
                    line->value = 1;
                }
            } break;
        }
    }

    pthread_mutex_unlock(&cbar->mutex);
}

void cbar_input(struct cbar *cbar, int id, int value)
{
    struct cbar_line *line = &cbar->lines[id];
    const struct cbar_line_config *config = &cbar->configs[id];

    assert(config->type == CROSSBAR_INPUT);
    printf("cbar: [input] %s set to %d\r\n", config->name, value);
    pthread_mutex_lock(&cbar->mutex);
    line->input.input_value = value;
    pthread_mutex_unlock(&cbar->mutex);
}

void cbar_post(struct cbar *cbar, int id)
{
    struct cbar_line *line = &cbar->lines[id];
    const struct cbar_line_config *config = &cbar->configs[id];

    assert(config->type == CROSSBAR_REQUEST);
    printf("cbar: [request] %s posted\r\n", config->name);
    pthread_mutex_lock(&cbar->mutex);
    line->value = 1;
    pthread_mutex_unlock(&cbar->mutex);
}

int cbar_value(struct cbar *cbar, int id)
{
    struct cbar_line *line = &cbar->lines[id];

    // Don't acquire the mutex as we will get called in the calculate function.
    return line->value;
}

bool cbar_pending(struct cbar *cbar, int id)
{
    struct cbar_line *line = &cbar->lines[id];
    const struct cbar_line_config *config = &cbar->configs[id];

    assert(config->type == CROSSBAR_REQUEST ||
           config->type == CROSSBAR_MONITOR ||
           config->type == CROSSBAR_PERIODIC);
    pthread_mutex_lock(&cbar->mutex);
    int value = line->value;
    line->value = 0;
    pthread_mutex_unlock(&cbar->mutex);
    if (value)
        printf("cbar: [request] %s pended\r\n", config->name);

    return value;
}

void cbar_dump(FILE *stream, struct cbar *cbar)
{
    for (int id=0; cbar->configs[id].type; id++) {
        struct cbar_line *line = &cbar->lines[id];
        const struct cbar_line_config *config = &cbar->configs[id];

        fprintf(stream, "cbar: %s = %d\r\n", config->name, line->value);
    }
}

/* vim: set ts=4 sw=4 et: */
