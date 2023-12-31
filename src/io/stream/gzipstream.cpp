// SPDX-License-Identifier: LGPL-2.1-or-later
/** @file
 * Zlib-enabled input and output streams
 *//*
 * Authors:
 * see git history
 * Bob Jamison <rjamison@titan.com>
 * 
 *
 * Copyright (C) 2018 Authors
 * Released under GNU LGPL v2.1+, read the file 'COPYING' for more information.
 */
/*
 * This is a thin wrapper of libz calls, in order
 * to provide a simple interface to our developers
 * for gzip input and output.
 */

#include "gzipstream.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Inkscape
{
namespace IO
{

//#########################################################################
//# G Z I P    I N P U T    S T R E A M
//#########################################################################

#define OUT_SIZE 4000

/**
 *
 */ 
GzipInputStream::GzipInputStream(InputStream &sourceStream)
                    : BasicInputStream(sourceStream),
                      loaded(false),
                      outputBuf(nullptr),
                      srcBuf(nullptr),
                      crc(0),
                      srcCrc(0),
                      srcSiz(0),
                      srcLen(0),
                      outputBufPos(0),
                      outputBufLen(0)
{
    memset( &d_stream, 0, sizeof(d_stream) );
}

/**
 *
 */ 
GzipInputStream::~GzipInputStream()
{
    close();
    if ( srcBuf ) {
      delete[] srcBuf;
      srcBuf = nullptr;
    }
    if ( outputBuf ) {
        delete[] outputBuf;
        outputBuf = nullptr;
    }
}

/**
 * Returns the number of bytes that can be read (or skipped over) from
 * this input stream without blocking by the next caller of a method for
 * this input stream.
 */ 
int GzipInputStream::available()
{
    if (closed || !outputBuf)
        return 0;
    return outputBufLen - outputBufPos;
}

    
/**
 *  Closes this input stream and releases any system resources
 *  associated with the stream.
 */ 
void GzipInputStream::close()
{
    if (closed)
        return;

    int zerr = inflateEnd(&d_stream);
    if (zerr != Z_OK) {
        printf("inflateEnd: Some kind of problem: %d\n", zerr);
    }

    if ( srcBuf ) {
      delete[] srcBuf;
      srcBuf = nullptr;
    }
    if ( outputBuf ) {
        delete[] outputBuf;
        outputBuf = nullptr;
    }
    closed = true;
}
    
/**
 * Reads the next byte of data from the input stream.  -1 if EOF
 */ 
int GzipInputStream::get()
{
    int ch = -1;
    if (closed) {
        // leave return value -1
    }
    else if (!loaded && !load()) {
        closed=true;
    } else {
        loaded = true;

        if ( outputBufPos >= outputBufLen ) {
            // time to read more, if we can
            fetchMore();
        }

        if ( outputBufPos < outputBufLen ) {
            ch = (int)outputBuf[outputBufPos++];
        }
    }

    return ch;
}

#define FTEXT 0x01
#define FHCRC 0x02
#define FEXTRA 0x04
#define FNAME 0x08
#define FCOMMENT 0x10

bool GzipInputStream::load()
{
    crc = crc32(0L, Z_NULL, 0);
    
    std::vector<Byte> inputBuf;
    while (true)
        {
        int ch = source.get();
        if (ch<0)
            break;
        inputBuf.push_back(static_cast<Byte>(ch & 0xff));
        }
    long inputBufLen = inputBuf.size();
    
    if (inputBufLen < 19) //header + tail + 1
        {
        return false;
        }

    srcLen = inputBuf.size();
    srcBuf = new (std::nothrow) Byte [srcLen];
    if (!srcBuf) {
        return false;
    }

    outputBuf = new (std::nothrow) unsigned char [OUT_SIZE];
    if ( !outputBuf ) {
        delete[] srcBuf;
        srcBuf = nullptr;
        return false;
    }
    outputBufLen = 0; // Not filled in yet

    std::vector<unsigned char>::iterator iter;
    Bytef *p = srcBuf;
    for (iter=inputBuf.begin() ; iter != inputBuf.end() ; ++iter)
	{
        *p++ = *iter;
	}

    size_t headerLen = 10;

    int flags = static_cast<int>(srcBuf[3]);

    constexpr size_t size_XLEN = 2;
    constexpr size_t size_CRC16 = 2;
    constexpr size_t size_CRC32 = 4;
    constexpr size_t size_ISIZE = 4;

    auto const check_not_truncated = [&] { return headerLen + size_CRC32 + size_ISIZE <= srcLen; };

    auto const skip_n = [&](size_t n) {
        headerLen += n;
        return check_not_truncated();
    };

    auto const skip_zero_terminated = [&] {
        while (headerLen < srcLen && srcBuf[headerLen++]) {
        }
        return check_not_truncated();
    };

    if (flags & FEXTRA) {
        if (!skip_n(size_XLEN)) {
            return false;
        }
        auto const xlen = size_t(srcBuf[headerLen - 2]) | //
                          size_t(srcBuf[headerLen - 1] << 8);
        if (!skip_n(xlen)) {
            return false;
        }
    }

    if ((flags & FNAME) && !skip_zero_terminated()) {
        return false;
    }

    if ((flags & FCOMMENT) && !skip_zero_terminated()) {
        return false;
    }

    if ((flags & FHCRC) && !skip_n(size_CRC16)) {
        return false;
    }

    if (!check_not_truncated()) {
        return false;
    }

    srcCrc = ((0x0ff & srcBuf[srcLen - 5]) << 24)
           | ((0x0ff & srcBuf[srcLen - 6]) << 16)
           | ((0x0ff & srcBuf[srcLen - 7]) <<  8)
           | ((0x0ff & srcBuf[srcLen - 8]) <<  0);
    //printf("srcCrc:%lx\n", srcCrc);
    
    srcSiz = ((0x0ff & srcBuf[srcLen - 1]) << 24)
           | ((0x0ff & srcBuf[srcLen - 2]) << 16)
           | ((0x0ff & srcBuf[srcLen - 3]) <<  8)
           | ((0x0ff & srcBuf[srcLen - 4]) <<  0);
    //printf("srcSiz:%lx/%ld\n", srcSiz, srcSiz);
    
    //outputBufLen = srcSiz + srcSiz/100 + 14;
    
    unsigned char *data = srcBuf + headerLen;
    unsigned long dataLen = srcLen - (headerLen + 8);
    //printf("%x %x\n", data[0], data[dataLen-1]);
    
    d_stream.zalloc    = (alloc_func)nullptr;
    d_stream.zfree     = (free_func)nullptr;
    d_stream.opaque    = (voidpf)nullptr;
    d_stream.next_in   = data;
    d_stream.avail_in  = dataLen;
    d_stream.next_out  = outputBuf;
    d_stream.avail_out = OUT_SIZE;
    
    int zerr = inflateInit2(&d_stream, -MAX_WBITS);
    if ( zerr == Z_OK )
    {
        zerr = fetchMore();
    } else {
        printf("inflateInit2: Some kind of problem: %d\n", zerr);
    }

        
    return (zerr == Z_OK) || (zerr == Z_STREAM_END);
}


int GzipInputStream::fetchMore()
{
    // TODO assumes we aren't called till the buffer is empty
    d_stream.next_out  = outputBuf;
    d_stream.avail_out = OUT_SIZE;
    outputBufLen = 0;
    outputBufPos = 0;

    int zerr = inflate( &d_stream, Z_SYNC_FLUSH );
    if ( zerr == Z_OK || zerr == Z_STREAM_END ) {
        outputBufLen = OUT_SIZE - d_stream.avail_out;
        if ( outputBufLen ) {
            crc = crc32(crc, const_cast<const Bytef *>(outputBuf), outputBufLen);
        }
        //printf("crc:%lx\n", crc);
//     } else if ( zerr != Z_STREAM_END ) {
//         // TODO check to be sure this won't happen for partial end reads
//         printf("inflate: Some kind of problem: %d\n", zerr);
    }

    return zerr;
}

//#########################################################################
//# G Z I P   O U T P U T    S T R E A M
//#########################################################################

/**
 *
 */ 
GzipOutputStream::GzipOutputStream(OutputStream &destinationStream)
                     : BasicOutputStream(destinationStream)
{

    totalIn         = 0;
    totalOut        = 0;
    crc             = crc32(0L, Z_NULL, 0);

    //Gzip header
    destination.put(0x1f);
    destination.put(0x8b);

    //Say it is compressed
    destination.put(Z_DEFLATED);

    //flags
    destination.put(0);

    //time
    destination.put(0);
    destination.put(0);
    destination.put(0);
    destination.put(0);

    //xflags
    destination.put(0);

    //OS code - from zutil.h
    //destination.put(OS_CODE);
    //apparently, we should not explicitly include zutil.h
    destination.put(0);

}

/**
 *
 */ 
GzipOutputStream::~GzipOutputStream()
{
    close();
}

/**
 * Closes this output stream and releases any system resources
 * associated with this stream.
 */ 
void GzipOutputStream::close()
{
    if (closed)
        return;

    flush();

    //# Send the CRC
    uLong outlong = crc;
    for (int n = 0; n < 4; n++)
        {
        destination.put(static_cast<char>(outlong & 0xff));
        outlong >>= 8;
        }
    //# send the file length
    outlong = totalIn & 0xffffffffL;
    for (int n = 0; n < 4; n++)
        {
        destination.put(static_cast<char>(outlong & 0xff));
        outlong >>= 8;
        }

    destination.close();
    closed = true;
}
    
/**
 *  Flushes this output stream and forces any buffered output
 *  bytes to be written out.
 */ 
void GzipOutputStream::flush()
{
    if (closed || inputBuf.empty())
	{
        return;
    }
	
    uLong srclen = inputBuf.size();
    Bytef *srcbuf = new (std::nothrow) Bytef [srclen];
    if (!srcbuf)
        {
        return;
        }
        
    uLong destlen = srclen;
    Bytef *destbuf = new (std::nothrow) Bytef [(destlen + (srclen/100) + 13)];
    if (!destbuf)
        {
        delete[] srcbuf;
        return;
        }
        
    std::vector<unsigned char>::iterator iter;
    Bytef *p = srcbuf;
    for (iter=inputBuf.begin() ; iter != inputBuf.end() ; ++iter)
        *p++ = *iter;
        
    crc = crc32(crc, const_cast<const Bytef *>(srcbuf), srclen);
    
    int zerr = compress(destbuf, static_cast<uLongf *>(&destlen), srcbuf, srclen);
    if (zerr != Z_OK)
        {
        printf("Some kind of problem\n");
        }

    totalOut += destlen;
    //skip the redundant zlib header and checksum
    for (uLong i=2; i<destlen-4 ; i++)
        {
        destination.put((int)destbuf[i]);
        }
        
    destination.flush();

    inputBuf.clear();
    delete[] srcbuf;
    delete[] destbuf;
}



/**
 * Writes the specified byte to this output stream.
 */ 
int GzipOutputStream::put(char ch)
{
    if (closed)
        {
        //probably throw an exception here
        return -1;
        }


    //Add char to buffer
    inputBuf.push_back(ch);
    totalIn++;
    return 1;
}



} // namespace IO
} // namespace Inkscape


//#########################################################################
//# E N D    O F    F I L E
//#########################################################################

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
