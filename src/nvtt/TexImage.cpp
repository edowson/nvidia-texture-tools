// Copyright NVIDIA Corporation 2007 -- Ignacio Castano <icastano@nvidia.com>
// 
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#include "TexImage.h"

#include "nvmath/Vector.h"
#include "nvmath/Matrix.h"
#include "nvmath/Color.h"

#include "nvimage/Filter.h"
#include "nvimage/ImageIO.h"
#include "nvimage/NormalMap.h"
#include "nvimage/BlockDXT.h"
#include "nvimage/ColorBlock.h"

#include <float.h>

using namespace nv;
using namespace nvtt;

namespace
{
	// 1 -> 1, 2 -> 2, 3 -> 2, 4 -> 4, 5 -> 4, ...
	static uint previousPowerOfTwo(const uint v)
	{
		return nextPowerOfTwo(v + 1) / 2;
	}

	static uint nearestPowerOfTwo(const uint v)
	{
		const uint np2 = nextPowerOfTwo(v);
		const uint pp2 = previousPowerOfTwo(v);

		if (np2 - v <= v - pp2)
		{
			return np2;
		}
		else
		{
			return pp2;
		}
	}

#pragma message(NV_FILE_LINE "TODO: Move these functions to a common location.")

	static int blockSize(Format format)
	{
		if (format == Format_DXT1 || format == Format_DXT1a || format == Format_DXT1n) {
			return 8;
		}
		else if (format == Format_DXT3) {
			return 16;
		}
		else if (format == Format_DXT5 || format == Format_DXT5n) {
			return 16;
		}
		else if (format == Format_BC4) {
			return 8;
		}
		else if (format == Format_BC5) {
			return 16;
		}
		else if (format == Format_CTX1) {
			return 8;
		}
		return 0;
	}

	static uint countMipmaps(int w, int h, int d)
	{
		uint mipmap = 0;

		while (w != 1 || h != 1 || d != 1) {
			w = max(1, w / 2);
			h = max(1, h / 2);
			d = max(1, d / 2);
			mipmap++;
		}

		return mipmap + 1;
	}
}


TexImage::TexImage() : m(new TexImage::Private())
{
}

TexImage::TexImage(const TexImage & tex) : m(tex.m)
{
	if (m != NULL) m->addRef();
}

TexImage::~TexImage()
{
	if (m != NULL) m->release();
	m = NULL;
}

void TexImage::operator=(const TexImage & tex)
{
	if (tex.m != NULL) tex.m->addRef();
	if (m != NULL) m->release();
	m = tex.m;
}

void TexImage::detach()
{
	if (m->refCount() > 1)
	{
        m->release();
		m = new TexImage::Private(*m);
		m->addRef();
		nvDebugCheck(m->refCount() == 1);
	}
}

void TexImage::setTextureType(TextureType type)
{
	if (m->type != type)
	{
		detach();

		m->type = type;

		int count = 0;
		if (type == TextureType_2D)
		{
			count = 1;
		}
		else
		{
			nvCheck (type == TextureType_Cube);
			count = 6;
		}

		// Delete all but the first 'count' images.
		const uint imageCount = m->imageArray.count();
		for (uint i = count; i < imageCount; i++)
		{
			delete m->imageArray[i];
		}

		m->imageArray.resize(count, NULL);
	}
}

void TexImage::setWrapMode(WrapMode wrapMode)
{
	if (m->wrapMode != wrapMode)
	{
		detach();
		m->wrapMode = wrapMode;
	}
}

void TexImage::setAlphaMode(AlphaMode alphaMode)
{
	if (m->alphaMode != alphaMode)
	{
		detach();
		m->alphaMode = alphaMode;
	}
}

void TexImage::setNormalMap(bool isNormalMap)
{
	if (m->isNormalMap != isNormalMap)
	{
		detach();
		m->isNormalMap = isNormalMap;
	}
}

int TexImage::width() const
{
	if (m->imageArray.count() > 0)
	{
		return m->imageArray[0]->width();
	}
	return 0;
}

int TexImage::height() const
{
	if (m->imageArray.count() > 0)
	{
		return m->imageArray[0]->height();
	}
	return 0;
}

int TexImage::depth() const
{
	return 1;
}

int TexImage::faceCount() const
{
	return m->imageArray.count();
}

TextureType TexImage::textureType() const
{
	return m->type;
}

WrapMode TexImage::wrapMode() const
{
	return m->wrapMode;
}

AlphaMode TexImage::alphaMode() const
{
	return m->alphaMode;
}

bool TexImage::isNormalMap() const
{
	return m->isNormalMap;
}

int TexImage::countMipmaps() const
{
	return ::countMipmaps(width(), height(), depth());
}

float TexImage::alphaTestCoverage(float alphaRef/*= 0.5*/) const
{
    int imageCount = 0;
    float coverage = 0.0f;

	foreach (i, m->imageArray)
	{
        FloatImage * img = m->imageArray[i];

		if (img == NULL) continue;

        imageCount++;
        coverage += img->alphaTestCoverage(alphaRef, 3);
	}

    if (imageCount > 0) {
        return coverage / imageCount;
    }
    else {
        return 0.0f;
    }
}


bool TexImage::load(const char * fileName)
{
#pragma message(NV_FILE_LINE "TODO: Add support for DDS textures in TexImage::load().")

	AutoPtr<FloatImage> img(ImageIO::loadFloat(fileName));

	if (img == NULL)
	{
		return false;
	}

	detach();

	img->resizeChannelCount(4);

	m->imageArray.resize(1);
	m->imageArray[0] = img.release();

	return true;
}

bool TexImage::save(const char * fileName) const
{
#pragma message(NV_FILE_LINE "TODO: Add support for DDS textures in TexImage::save")

	if (m->imageArray.count() == 0)
	{
		return false;
	}
	else
	{
		return ImageIO::saveFloat(fileName, m->imageArray[0], 0, 4);
	}
}

bool TexImage::setImage2D(nvtt::InputFormat format, int w, int h, int idx, const void * restrict data)
{
	if (idx < 0 || uint(idx) >= m->imageArray.count())
	{
		return false;
	}

	FloatImage * img = m->imageArray[idx];
	if (img->width() != w || img->height() != h)
	{
		return false;
	}

	detach();

	const int count = w * h;

	float * restrict rdst = img->channel(0);
	float * restrict gdst = img->channel(1);
	float * restrict bdst = img->channel(2);
	float * restrict adst = img->channel(3);

	if (format == InputFormat_BGRA_8UB)
	{
		const Color32 * src = (const Color32 *)data;

		try {
			for (int i = 0; i < count; i++)
			{
				rdst[i] = src[i].r;
				gdst[i] = src[i].g;
				bdst[i] = src[i].b;
				adst[i] = src[i].a;
			}
		}
		catch(...) {
			return false;
		}
	}
	else if (format == InputFormat_RGBA_32F)
	{
		const float * src = (const float *)data;

		try {
			for (int i = 0; i < count; i++)
			{
				rdst[i] = src[4 * i + 0];
				gdst[i] = src[4 * i + 1];
				bdst[i] = src[4 * i + 2];
				adst[i] = src[4 * i + 3];
			}
		}
		catch(...) {
			return false;
		}
	}

	return true;
}

bool TexImage::setImage2D(InputFormat format, int w, int h, int idx, const void * restrict r, const void * restrict g, const void * restrict b, const void * restrict a)
{
	if (idx < 0 || uint(idx) >= m->imageArray.count())
	{
		return false;
	}

	FloatImage * img = m->imageArray[idx];
	if (img->width() != w || img->height() != h)
	{
		return false;
	}

	detach();

	const int count = w * h;

	float * restrict rdst = img->channel(0);
	float * restrict gdst = img->channel(1);
	float * restrict bdst = img->channel(2);
	float * restrict adst = img->channel(3);

	if (format == InputFormat_BGRA_8UB)
	{
		const uint8 * restrict rsrc = (const uint8 *)r;
		const uint8 * restrict gsrc = (const uint8 *)g;
		const uint8 * restrict bsrc = (const uint8 *)b;
		const uint8 * restrict asrc = (const uint8 *)a;

		try {
			for (int i = 0; i < count; i++) rdst[i] = float(rsrc[i]) / 255.0f;
			for (int i = 0; i < count; i++) gdst[i] = float(gsrc[i]) / 255.0f;
			for (int i = 0; i < count; i++) bdst[i] = float(bsrc[i]) / 255.0f;
			for (int i = 0; i < count; i++) adst[i] = float(asrc[i]) / 255.0f;
		}
		catch(...) {
			return false;
		}
	}
	else if (format == InputFormat_RGBA_32F)
	{
		const float * rsrc = (const float *)r;
		const float * gsrc = (const float *)g;
		const float * bsrc = (const float *)b;
		const float * asrc = (const float *)a;

		try {
			memcpy(rdst, rsrc, count * sizeof(float));
			memcpy(gdst, gsrc, count * sizeof(float));
			memcpy(bdst, bsrc, count * sizeof(float));
			memcpy(adst, asrc, count * sizeof(float));
		}
		catch(...) {
			return false;
		}
	}

	return true;
}

bool TexImage::setImage2D(Format format, Decoder decoder, int w, int h, int idx, const void * data)
{
	if (idx < 0 || uint(idx) >= m->imageArray.count())
	{
		return false;
	}

#pragma message(NV_FILE_LINE "TODO: Add support for all compressed formats in TexImage::setImage2D.")

	if (format != nvtt::Format_BC1 && format != nvtt::Format_BC2 && format != nvtt::Format_BC3)
	{
		return false;
	}

	FloatImage * img = m->imageArray[idx];
	if (img->width() != w || img->height() != h)
	{
		return false;
	}

	detach();

	const int count = w * h;

	const int bw = (w + 3) / 4;
	const int bh = (h + 3) / 4;

	const uint bs = blockSize(format);

	const uint8 * ptr = (const uint8 *)data;

	try {
		for (int y = 0; y < bh; y++)
		{
			for (int x = 0; x < bw; x++)
			{
				ColorBlock colors;

				if (format == nvtt::Format_BC1)
				{
					const BlockDXT1 * block = (const BlockDXT1 *)ptr;

					if (decoder == Decoder_Reference) {
						block->decodeBlock(&colors);
					}
					else if (decoder == Decoder_NV5x) {
						block->decodeBlockNV5x(&colors);
					}
				}
				else if (format == nvtt::Format_BC2)
				{
					const BlockDXT3 * block = (const BlockDXT3 *)ptr;

					if (decoder == Decoder_Reference) {
						block->decodeBlock(&colors);
					}
					else if (decoder == Decoder_NV5x) {
						block->decodeBlockNV5x(&colors);
					}
				}
				else if (format == nvtt::Format_BC3)
				{
					const BlockDXT5 * block = (const BlockDXT5 *)ptr;

					if (decoder == Decoder_Reference) {
						block->decodeBlock(&colors);
					}
					else if (decoder == Decoder_NV5x) {
						block->decodeBlockNV5x(&colors);
					}
				}

				for (int yy = 0; yy < 4; yy++)
				{
					for (int xx = 0; xx < 4; xx++)
					{
						Color32 c = colors.color(xx, yy);

						if (x * 4 + xx < w && y * 4 + yy < h)
						{
							img->pixel(x*4 + xx, y*4 + yy, 0) = float(c.r) * 1.0f/255.0f;
							img->pixel(x*4 + xx, y*4 + yy, 1) = float(c.g) * 1.0f/255.0f;
							img->pixel(x*4 + xx, y*4 + yy, 2) = float(c.b) * 1.0f/255.0f;
							img->pixel(x*4 + xx, y*4 + yy, 3) = float(c.a) * 1.0f/255.0f;
						}
					}
				}

				ptr += bs;
			}
		}
	}
	catch(...) {
		return false;
	}

	return true;
}


#pragma message(NV_FILE_LINE "TODO: provide a TexImage::resize that can override filter width and parameters.")

void TexImage::resize(int w, int h, ResizeFilter filter)
{
	if (m->imageArray.count() > 0)
	{
		if (w == m->imageArray[0]->width() && h == m->imageArray[0]->height()) return;
	}

	if (m->type == TextureType_Cube)
	{
#pragma message(NV_FILE_LINE "TODO: Output error when image is cubemap and w != h.")
		h = w;
	}

	detach();

	FloatImage::WrapMode wrapMode = (FloatImage::WrapMode)m->wrapMode;

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];

		if (img == NULL) continue;

		if (m->alphaMode == AlphaMode_Transparency)
		{
			if (filter == ResizeFilter_Box)
			{
				BoxFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode, 3);
			}
			else if (filter == ResizeFilter_Triangle)
			{
				TriangleFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode, 3);
			}
			else if (filter == ResizeFilter_Kaiser)
			{
				//KaiserFilter filter(inputOptions.kaiserWidth);
				//filter.setParameters(inputOptions.kaiserAlpha, inputOptions.kaiserStretch);
				KaiserFilter filter(3);
				m->imageArray[i]->resize(filter, w, h, wrapMode, 3);
			}
			else //if (filter == ResizeFilter_Mitchell)
			{
				nvDebugCheck(filter == ResizeFilter_Mitchell);
				MitchellFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode, 3);
			}
		}
		else
		{
			if (filter == ResizeFilter_Box)
			{
				BoxFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode);
			}
			else if (filter == ResizeFilter_Triangle)
			{
				TriangleFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode);
			}
			else if (filter == ResizeFilter_Kaiser)
			{
				//KaiserFilter filter(inputOptions.kaiserWidth);
				//filter.setParameters(inputOptions.kaiserAlpha, inputOptions.kaiserStretch);
				KaiserFilter filter(3);
				m->imageArray[i]->resize(filter, w, h, wrapMode);
			}
			else //if (filter == ResizeFilter_Mitchell)
			{
				nvDebugCheck(filter == ResizeFilter_Mitchell);
				MitchellFilter filter;
				m->imageArray[i]->resize(filter, w, h, wrapMode);
			}
		}
	}
}

void TexImage::resize(int maxExtent, RoundMode roundMode, ResizeFilter filter)
{
	if (m->imageArray.count() > 0)
	{
		int w = m->imageArray[0]->width();
		int h = m->imageArray[0]->height();

		nvDebugCheck(w > 0);
		nvDebugCheck(h > 0);

		if (roundMode != RoundMode_None)
		{
			// rounded max extent should never be higher than original max extent.
			maxExtent = previousPowerOfTwo(maxExtent);
		}

		// Scale extents without changing aspect ratio.
		int maxwh = max(w, h);
		if (maxExtent != 0 && maxwh > maxExtent)
		{
			w = max((w * maxExtent) / maxwh, 1);
			h = max((h * maxExtent) / maxwh, 1);
		}

		// Round to power of two.
		if (roundMode == RoundMode_ToNextPowerOfTwo)
		{
			w = nextPowerOfTwo(w);
			h = nextPowerOfTwo(h);
		}
		else if (roundMode == RoundMode_ToNearestPowerOfTwo)
		{
			w = nearestPowerOfTwo(w);
			h = nearestPowerOfTwo(h);
		}
		else if (roundMode == RoundMode_ToPreviousPowerOfTwo)
		{
			w = previousPowerOfTwo(w);
			h = previousPowerOfTwo(h);
		}

		// Make sure cube faces are square.
		if (m->type == TextureType_Cube)
		{
			w = h = max(w, h);
		}

		resize(w, h, filter);
	}
}

bool TexImage::buildNextMipmap(MipmapFilter filter)
{
	if (m->imageArray.count() > 0)
	{
		int w = m->imageArray[0]->width();
		int h = m->imageArray[0]->height();

		nvDebugCheck(w > 0);
		nvDebugCheck(h > 0);

		if (w == 1 && h == 1)
		{
			return false;
		}
	}

	detach();

	FloatImage::WrapMode wrapMode = (FloatImage::WrapMode)m->wrapMode;

	foreach (i, m->imageArray)
	{
        FloatImage * img = m->imageArray[i];

		if (img == NULL) continue;

		if (m->alphaMode == AlphaMode_Transparency)
		{
			if (filter == MipmapFilter_Box)
			{
				BoxFilter filter;
				img = img->downSample(filter, wrapMode, 3);
			}
			else if (filter == MipmapFilter_Triangle)
			{
				TriangleFilter filter;
				img = img->downSample(filter, wrapMode, 3);
			}
			else if (filter == MipmapFilter_Kaiser)
			{
				nvDebugCheck(filter == MipmapFilter_Kaiser);
				//KaiserFilter filter(inputOptions.kaiserWidth);
				//filter.setParameters(inputOptions.kaiserAlpha, inputOptions.kaiserStretch);
				KaiserFilter filter(3);
				img = img->downSample(filter, wrapMode, 3);
			}
		}
		else
		{
			if (filter == MipmapFilter_Box)
			{
				img = img->fastDownSample();
			}
			else if (filter == MipmapFilter_Triangle)
			{
				TriangleFilter filter;
				img = img->downSample(filter, wrapMode);
			}
			else //if (filter == MipmapFilter_Kaiser)
			{
				nvDebugCheck(filter == MipmapFilter_Kaiser);
				//KaiserFilter filter(inputOptions.kaiserWidth);
				//filter.setParameters(inputOptions.kaiserAlpha, inputOptions.kaiserStretch);
				KaiserFilter filter(3);
				img = img->downSample(filter, wrapMode);
			}
		}

        delete m->imageArray[i];
        m->imageArray[i] = img;
	}

	return true;
}

// Color transforms.
void TexImage::toLinear(float gamma)
{
	if (equal(gamma, 1.0f)) return;

	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		m->imageArray[i]->toLinear(0, 3, gamma);
	}
}

void TexImage::toGamma(float gamma)
{
	if (equal(gamma, 1.0f)) return;

	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		m->imageArray[i]->toGamma(0, 3, gamma);
	}
}

void TexImage::transform(const float w0[4], const float w1[4], const float w2[4], const float w3[4], const float offset[4])
{
	detach();

	Matrix xform(
		Vector4(w0[0], w0[1], w0[2], w0[3]),
		Vector4(w1[0], w1[1], w1[2], w1[3]),
		Vector4(w2[0], w2[1], w2[2], w2[3]),
		Vector4(w3[0], w3[1], w3[2], w3[3]));

	Vector4 voffset(offset[0], offset[1], offset[2], offset[3]);

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		m->imageArray[i]->transform(0, xform, voffset);
	}
}

void TexImage::swizzle(int r, int g, int b, int a)
{
	if (r == 0 && g == 1 && b == 2 && a == 3) return;

	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		m->imageArray[i]->swizzle(0, r, g, b, a);
	}
}

void TexImage::scaleBias(int channel, float scale, float bias)
{
	if (equal(scale, 1.0f) && equal(bias, 0.0f)) return;

	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		m->imageArray[i]->scaleBias(channel, 1, scale, bias);
	}
}

void TexImage::packNormal()
{
	scaleBias(0, 0.5f, 0.5f);
	scaleBias(1, 0.5f, 0.5f);
	scaleBias(2, 0.5f, 0.5f);
}

void TexImage::expandNormal()
{
	scaleBias(0, 2.0f, -1.0f);
	scaleBias(1, 2.0f, -1.0f);
	scaleBias(2, 2.0f, -1.0f);
}


void TexImage::blend(float red, float green, float blue, float alpha, float t)
{
	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

		float * restrict r = img->channel(0);
		float * restrict g = img->channel(1);
		float * restrict b = img->channel(2);
		float * restrict a = img->channel(3);

		const int count = img->width() * img->height();
		for (int i = 0; i < count; i++)
		{
			r[i] = lerp(r[i], red, t);
			g[i] = lerp(g[i], green, t);
			b[i] = lerp(b[i], blue, t);
			a[i] = lerp(a[i], alpha, t);
		}
	}
}

void TexImage::premultiplyAlpha()
{
	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

		float * restrict r = img->channel(0);
		float * restrict g = img->channel(1);
		float * restrict b = img->channel(2);
		float * restrict a = img->channel(3);

		const int count = img->width() * img->height();
		for (int i = 0; i < count; i++)
		{
			r[i] *= a[i];
			g[i] *= a[i];
			b[i] *= a[i];
		}
	}
}


void TexImage::toGreyScale(float redScale, float greenScale, float blueScale, float alphaScale)
{
	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

		float sum = redScale + greenScale + blueScale + alphaScale;
		redScale /= sum;
		greenScale /= sum;
		blueScale /= sum;
		alphaScale /= sum;

		float * restrict r = img->channel(0);
		float * restrict g = img->channel(1);
		float * restrict b = img->channel(2);
		float * restrict a = img->channel(3);

		const int count = img->width() * img->height();
		for (int i = 0; i < count; i++)
		{
			float grey = r[i] * redScale + g[i] * greenScale + b[i] * blueScale + a[i] * alphaScale;
			a[i] = b[i] = g[i] = r[i] = grey;
		}
	}
}

// Draw colored border.
void TexImage::setBorder(float r, float g, float b, float a)
{
	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

		const int w = img->width();
		const int h = img->height();

		for (int i = 0; i < w; i++)
		{
			img->pixel(i, 0, 0) = r;
			img->pixel(i, 0, 1) = g;
			img->pixel(i, 0, 2) = b;
			img->pixel(i, 0, 3) = a;

			img->pixel(i, h-1, 0) = r;
			img->pixel(i, h-1, 1) = g;
			img->pixel(i, h-1, 2) = b;
			img->pixel(i, h-1, 3) = a;
		}

		for (int i = 0; i < h; i++)
		{
			img->pixel(0, i, 0) = r;
			img->pixel(0, i, 1) = g;
			img->pixel(0, i, 2) = b;
			img->pixel(0, i, 3) = a;

			img->pixel(w-1, i, 0) = r;
			img->pixel(w-1, i, 1) = g;
			img->pixel(w-1, i, 2) = b;
			img->pixel(w-1, i, 3) = a;
		}
	}
}

// Fill image with the given color.
void TexImage::fill(float red, float green, float blue, float alpha)
{
	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

		float * restrict r = img->channel(0);
		float * restrict g = img->channel(1);
		float * restrict b = img->channel(2);
		float * restrict a = img->channel(3);

		const int count = img->width() * img->height();
		for (int i = 0; i < count; i++)
		{
			r[i] = red;
			g[i] = green;
			b[i] = blue;
			a[i] = alpha;
		}
	}
}


void TexImage::scaleAlphaToCoverage(float coverage, float alphaRef/*= 0.5f*/)
{
    detach();

	foreach (i, m->imageArray)
	{
		FloatImage * img = m->imageArray[i];
		if (img == NULL) continue;

        img->scaleAlphaToCoverage(coverage, alphaRef, 3);
    }
}


// Set normal map options.
void TexImage::toNormalMap(float sm, float medium, float big, float large)
{
	detach();

	const Vector4 filterWeights(sm, medium, big, large);

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		const FloatImage * img = m->imageArray[i];
		m->imageArray[i] = nv::createNormalMap(img, (FloatImage::WrapMode)m->wrapMode, filterWeights);

#pragma message(NV_FILE_LINE "TODO: Pack and expand normals explicitly?")
		m->imageArray[i]->packNormals(0);

		delete img;
	}

	m->isNormalMap = true;
}

/*void TexImage::toHeightMap()
{
	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

#pragma message(NV_FILE_LINE "TODO: Implement TexImage::toHeightMap")
	}

	m->isNormalMap = false;
}*/

void TexImage::normalizeNormalMap()
{
	//nvCheck(m->isNormalMap);

	detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

		nv::normalizeNormalMap(m->imageArray[i]);
	}
}

float TexImage::rootMeanSquaredError_rgb(const TexImage & reference) const
{
	int totalCount = 0;
	double mse = 0;

	const int faceCount = this->faceCount();
	if (faceCount != reference.faceCount()) {
		return FLT_MAX;
	}

	for (int f = 0; f < faceCount; f++)
	{
		const FloatImage * img = m->imageArray[f];
		const FloatImage * ref = reference.m->imageArray[f];

		if (img == NULL || ref == NULL) {
			return FLT_MAX;
		}

		nvCheck(img->componentNum() == 4);
		nvCheck(ref->componentNum() == 4);

		const uint count = img->width() * img->height();
		totalCount += count;

		for (uint i = 0; i < count; i++)
		{
			float r0 = img->pixel(4 * i + count * 0);
			float g0 = img->pixel(4 * i + count * 1);
			float b0 = img->pixel(4 * i + count * 2);
			float a0 = img->pixel(4 * i + count * 3);
			float r1 = ref->pixel(4 * i + count * 0);
			float g1 = ref->pixel(4 * i + count * 1);
			float b1 = ref->pixel(4 * i + count * 2);
			float a1 = ref->pixel(4 * i + count * 3);

			float r = r0 - r1;
			float g = g0 - g1;
			float b = b0 - b1;
			float a = a0 - a1;

			if (reference.alphaMode() == nvtt::AlphaMode_Transparency)
			{
				mse += double(r * r * a1) / 255.0;
				mse += double(g * g * a1) / 255.0;
				mse += double(b * b * a1) / 255.0;
			}
			else
			{
				mse += r * r;
				mse += g * g;
				mse += b * b;
			}
		}
	}

	return float(sqrt(mse / totalCount));
}

float TexImage::rootMeanSquaredError_alpha(const TexImage & reference) const
{
	int totalCount = 0;
	double mse = 0;

	const int faceCount = this->faceCount();
	if (faceCount != reference.faceCount()) {
		return FLT_MAX;
	}

	for (int f = 0; f < faceCount; f++)
	{
		const FloatImage * img = m->imageArray[f];
		const FloatImage * ref = reference.m->imageArray[f];

		if (img == NULL || ref == NULL) {
			return FLT_MAX;
		}

		nvCheck(img->componentNum() == 4);
		nvCheck(ref->componentNum() == 4);

		const uint count = img->width() * img->height();
		totalCount += count;

		for (uint i = 0; i < count; i++)
		{
			float a0 = img->pixel(4 * i + count * 3);
			float a1 = ref->pixel(4 * i + count * 3);

			float a = a0 - a1;

			mse += a * a;
		}
	}

	return float(sqrt(mse / totalCount));
}

void TexImage::flipVertically()
{
    detach();

	foreach (i, m->imageArray)
	{
		if (m->imageArray[i] == NULL) continue;

        m->imageArray[i]->flip();
	}
}

bool TexImage::copyChannel(const TexImage & srcImage, int srcChannel)
{
	return copyChannel(srcImage, srcChannel, srcChannel);
}

bool TexImage::copyChannel(const TexImage & srcImage, int srcChannel, int dstChannel)
{
	const int faceCount = this->faceCount();
	if (faceCount != srcImage.faceCount()) {
		return false;
	}

	detach();

	foreach (i, m->imageArray)
	{
		FloatImage * dst = m->imageArray[i];
		const FloatImage * src = srcImage.m->imageArray[i];

		if (dst == NULL || src == NULL) {
			return false;
		}

		nvCheck(src->componentNum() == 4);
		nvCheck(dst->componentNum() == 4);

		const uint w = src->width();
		const uint h = src->height();

		if (w != dst->width() || h != dst->height()) {
			return false;
		}

		memcpy(dst->channel(dstChannel), src->channel(srcChannel), w*h*sizeof(float));
	}

	return true;
}

