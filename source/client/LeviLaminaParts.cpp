// LeviLamina's own small classes that MyClient used from LeviLamina.dll, written again here (2026-11-06, step 4 of
// leaving LeviLamina on 26.51): mce::Blob and mce::Image (a skin's picture). They are not the game's code:
// LeviLamina 26.51.6 implements them in src/mc/deps/core/container/Blob.cpp and image/Image.cpp on the layouts of
// its headers, which MyClient keeps using.
//
// The build defines LLAPI as nothing (tools/build_client.py), so the headers' LLAPI functions are ordinary
// functions: these definitions are used, the rest still come from LeviLamina.lib until they are replaced too.
//
// A Blob's bytes are freed by the function stored in its Deleter (m_func), so a buffer made here is freed here
// (std::free) even when the game drops the Blob, and one made by the game is freed by the game's function.
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/image/Image.h"

namespace mce {

// --- Blob ---

Blob::Deleter::Deleter() noexcept : m_func(&Blob::defaultDeleter) {}
Blob::Deleter::Deleter(delete_function f) noexcept : m_func(f ? f : &Blob::defaultDeleter) {}
void Blob::Deleter::operator()(value_type* p) const noexcept
{
    if (p && m_func) m_func(p);
}

void Blob::defaultDeleter(value_type* p) noexcept { std::free(p); }

Blob::Blob() noexcept : mBlob(nullptr, Deleter()), mSize(0) {}

Blob::Blob(size_type size) : mBlob(nullptr, Deleter()), mSize(0)
{
    if (!size) return;
    auto* buffer = static_cast<value_type*>(std::calloc(size, 1));
    if (!buffer) throw std::bad_alloc();
    mBlob.reset(buffer);
    mSize = size;
}

Blob::Blob(value_type const* ptr, size_type size) : Blob(size)
{
    if (ptr && size && mBlob) std::memcpy(mBlob.get(), ptr, size);
}

Blob::Blob(Blob const& rhs) : Blob(rhs.mBlob.get(), rhs.mBlob ? rhs.mSize : 0) {}

Blob::Blob(Blob&& rhs) noexcept : mBlob(std::move(rhs.mBlob)), mSize(rhs.mSize) { rhs.mSize = 0; }

Blob::Blob(pointer_type&& ptr, size_type size) noexcept : mBlob(std::move(ptr)), mSize(size) {}

Blob::~Blob() = default;

Blob& Blob::operator=(Blob const& rhs)
{
    if (this != &rhs)
    {
        Blob copy(rhs);
        swap(copy);
    }
    return *this;
}

Blob& Blob::operator=(Blob&& rhs) noexcept
{
    if (this != &rhs)
    {
        mBlob = std::move(rhs.mBlob);   // frees the old bytes with their own deleter, takes rhs's deleter
        mSize = rhs.mSize;
        rhs.mSize = 0;
    }
    return *this;
}

Blob::iterator Blob::begin() noexcept { return mBlob.get(); }
Blob::iterator Blob::end() noexcept { return mBlob.get() + mSize; }
Blob::const_iterator Blob::cbegin() const noexcept { return mBlob.get(); }
Blob::const_iterator Blob::cend() const noexcept { return mBlob.get() + mSize; }
bool Blob::empty() const noexcept { return mSize == 0; }
Blob::size_type Blob::size() const noexcept { return mSize; }
Blob::size_type Blob::max_size() const noexcept { return static_cast<size_type>(-1); }
Blob::pointer Blob::data() noexcept { return mBlob.get(); }
Blob::const_pointer Blob::data() const noexcept { return mBlob.get(); }
Blob::pointer Blob::get() noexcept { return mBlob.get(); }
Blob::const_pointer Blob::get() const noexcept { return mBlob.get(); }

Blob::pointer_type Blob::release() noexcept
{
    mSize = 0;
    return std::move(mBlob);
}

void Blob::fillBlob(std::uint8_t val)
{
    if (mBlob && mSize) std::memset(mBlob.get(), val, mSize);
}

void Blob::swap(Blob& other) noexcept
{
    std::swap(mBlob, other.mBlob);
    std::swap(mSize, other.mSize);
}

// --- Image ---

Image::Image()
: imageFormat(ImageFormat::Unknown), mWidth(0), mHeight(0), mDepth(1), mUsage(ImageUsage::Unknown), mAlphaUsage{}, mImageBytes()
{
}

Image::Image(Image const& other)
: imageFormat(other.imageFormat), mWidth(other.mWidth), mHeight(other.mHeight), mDepth(other.mDepth),
  mUsage(other.mUsage), mAlphaUsage(other.mAlphaUsage), mImageBytes(other.mImageBytes)
{
}

Image::Image(Image&& other)
: imageFormat(other.imageFormat), mWidth(other.mWidth), mHeight(other.mHeight), mDepth(other.mDepth),
  mUsage(other.mUsage), mAlphaUsage(other.mAlphaUsage), mImageBytes(std::move(other.mImageBytes))
{
}

Image::~Image() = default;

Image& Image::operator=(Image const& other)
{
    if (this != &other)
    {
        imageFormat = other.imageFormat;
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mDepth = other.mDepth;
        mUsage = other.mUsage;
        mAlphaUsage = other.mAlphaUsage;
        mImageBytes = other.mImageBytes;
    }
    return *this;
}

Image& Image::operator=(Image&& other)
{
    if (this != &other)
    {
        imageFormat = other.imageFormat;
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mDepth = other.mDepth;
        mUsage = other.mUsage;
        mAlphaUsage = other.mAlphaUsage;
        mImageBytes = std::move(other.mImageBytes);
    }
    return *this;
}

bool Image::isEmpty() const { return mImageBytes.empty(); }

}  // namespace mce
