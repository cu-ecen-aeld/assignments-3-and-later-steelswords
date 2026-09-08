/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include "aesd-circular-buffer.h"
#define AESD_DEBUG 1  //Remove comment on this line to enable debug
#include "pdebug.h"

#include <linux/mutex.h>
#include <linux/rwsem.h>

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
    struct cdev cdev;     /* Char device structure      */
    struct mutex lock;
    struct aesd_circular_buffer stored_circular_buffer;
    struct aesd_circular_buffer receive_circular_buffer; // Is 10 going to be sufficient?
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
