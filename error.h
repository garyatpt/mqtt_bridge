/*
* The MIT License (MIT)
*
* Copyright (c) 2013, Marcelo Aquino, https://github.com/mapnull
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#ifndef ERROR_H
#define ERROR_H

#define ERROR_INV_DATA 0			// Invalid message format
#define ERROR_INV_PROTOCOL 1		// Invalid protocol number
#define ERROR_MIS_PROTOCOL 2		// Missing protocol code
#define ERROR_MIS_DEVICE_ID 3		// Missing device id
#define ERROR_MIS_MODULE_ID 4		// Missing module id
#define ERROR_DEV_POWER_FAILURE 5	// Power failure
#define ERROR_DEV_HARDWARE 6		// Device hardware error
#define ERROR_DEV_NOT_READY 7		// Device not ready
#define ERROR_DEV_INV_ID 8			// Invalid device id
#define ERROR_MD_NOT_FOUND 9		// Module not found
#define ERROR_MD_NOT_AVAILABLE 10	// Module not available
#define ERROR_MD_DISABLED 11		// Module is disabled
#define ERROR_MD_HARDWARE 12		// Module hardware error
#define ERROR_MD_INV_ID 13			// Invalid module id
#define ERROR_MD_INV_INFO 14		// Invalid module info
#define ERROR_MD_INV_TOPIC 15		// Invalid module topic
#define ERROR_MD_INV_SPECS 16		// Invalid module specs
#define ERROR_MD_INV_OPTS 17		// Invalid module options
#define ERROR_MD_NOT_IPM 18			// Not implemented function

#endif
