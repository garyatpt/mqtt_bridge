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

#define ERROR_OK 0						// No error
#define ERROR_UNKNOWN 1					// Invalid message format
#define ERROR_INV_DATA 2				// Invalid message format
#define ERROR_DEV_POWER_FAILURE 3		// Power failure
#define ERROR_DEV_HARDWARE 4			// Device hardware error
#define ERROR_DEV_NOT_READY 5			// Device not ready
#define ERROR_DEV_INV_UUID 6			// Invalid device uuid
#define ERROR_NOT_IPM 7					// Not implemented function
#define ERROR_TIMEOUT 8
#define ERROR_UNKNOWN_KEY 9

#endif
