#include "serialise.hh"
#include "util.hh"

#include <cstring>
#include <cerrno>
#include <memory>


namespace nix {


void BufferedSink::operator () (const unsigned char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new unsigned char[bufSize]);

    while (len) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (bufPos + len >= bufSize) {
            flush();
            write(data, len);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + len > bufSize ? bufSize - bufPos : len;
        memcpy(buffer.get() + bufPos, data, n);
        data += n; bufPos += n; len -= n;
        if (bufPos == bufSize) flush();
    }
}


void BufferedSink::flush()
{
    if (bufPos == 0) return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    write(buffer.get(), n);
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreException(); }
}


size_t threshold = 256 * 1024 * 1024;

static void warnLargeDump()
{
    printError("warning: dumping very large path (> 256 MiB); this may run out of memory");
}


void FdSink::write(const unsigned char * data, size_t len)
{
    written += len;
    static bool warned = false;
    if (warn && !warned) {
        if (written > threshold) {
            warnLargeDump();
            warned = true;
        }
    }
    try {
        writeFull(fd, data, len);
    } catch (SysError & e) {
        _good = true;
    }
}


bool FdSink::good()
{
    return _good;
}


void Source::operator () (unsigned char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n; len -= n;
    }
}


size_t BufferedSource::read(unsigned char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new unsigned char[bufSize]);

    if (!bufPosIn) bufPosIn = readUnbuffered(buffer.get(), bufSize);

    /* Copy out the data in the buffer. */
    size_t n = len > bufPosIn - bufPosOut ? bufPosIn - bufPosOut : len;
    memcpy(data, buffer.get() + bufPosOut, n);
    bufPosOut += n;
    if (bufPosIn == bufPosOut) bufPosIn = bufPosOut = 0;
    return n;
}


bool BufferedSource::hasData()
{
    return bufPosOut < bufPosIn;
}


size_t FdSource::readUnbuffered(unsigned char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, (char *) data, bufSize);
    } while (n == -1 && errno == EINTR);
    if (n == -1) { _good = false; throw SysError("reading from file"); }
    if (n == 0) { _good = false; throw EndOfFile("unexpected end-of-file"); }
    read += n;
    return n;
}


bool FdSource::good()
{
    return _good;
}


size_t StringSource::read(unsigned char * data, size_t len)
{
    if (pos == s.size()) throw EndOfFile("end of string reached");
    size_t n = s.copy((char *) data, len, pos);
    pos += n;
    return n;
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


void writeString(const unsigned char * buf, size_t len, Sink & sink)
{
    sink << len;
    sink(buf, len);
    writePadding(len, sink);
}


Sink & operator << (Sink & sink, const string & s)
{
    writeString((const unsigned char *) s.data(), s.size(), sink);
    return sink;
}


template<class T> void writeStrings(const T & ss, Sink & sink)
{
    sink << ss.size();
    for (auto & i : ss)
        sink << i;
}

Sink & operator << (Sink & sink, const Strings & s)
{
    writeStrings(s, sink);
    return sink;
}

Sink & operator << (Sink & sink, const StringSet & s)
{
    writeStrings(s, sink);
    return sink;
}


void readPadding(size_t len, Source & source)
{
    if (len % 8) {
        unsigned char zero[8];
        size_t n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


size_t readString(unsigned char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw Error("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}


string readString(Source & source)
{
    auto len = readNum<size_t>(source);
    std::string res(len, 0);
    source((unsigned char*) res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator >> (Source & in, string & s)
{
    s = readString(in);
    return in;
}


template<class T> T readStrings(Source & source)
{
    auto count = readNum<size_t>(source);
    T ss;
    while (count--)
        ss.insert(ss.end(), readString(source));
    return ss;
}

template Paths readStrings(Source & source);
template PathSet readStrings(Source & source);


void StringSink::operator () (const unsigned char * data, size_t len)
{
    static bool warned = false;
    if (!warned && s->size() > threshold) {
        warnLargeDump();
        warned = true;
    }
    s->append((const char *) data, len);
}


}
