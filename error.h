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

typedef enum {
	ERROR_OK = 0,
	ERROR_UNKNOWN,
	ERROR_INV_DATA,				// Invalid message format
	ERROR_INV_JSON,				// Invalid json
	ERROR_UNKNOWN_JSON,			// Unknown json
	ERROR_UNKNOWN_JSON_KEY,		// Unknown json key
	ERROR_INV_VALUE,			// Invalid value
	ERROR_MIS_UUID,				// Missing uuid
	ERROR_POWER_FAILURE,		// Power failure
	ERROR_HARDWARE,				// Hardware error
	ERROR_NOT_READY,			// Not ready
	ERROR_NOT_IPM,				// Not implemented function
	ERROR_TIMEOUT
} error_t;

#endif
