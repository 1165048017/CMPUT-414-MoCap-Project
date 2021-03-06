#include "Texture.hpp"
#include "Graphics.hpp"
#include "GLExtensions.hpp"

#include <png.h>

#include <iostream>

using std::cerr;
using std::endl;

namespace Graphics
{

CachedTexture::CachedTexture(string const &_filename, bool _pad, bool _flip) : filename(_filename), pad(_pad), flip(_flip)
{
  id = TEXTURE_ID;
  obj = 0;
  w = h = 0;
  tw = th = 0;
  loaded = false;
  reload();
}

CachedTexture::~CachedTexture()
{
  if (loaded && obj)
  {
    glDeleteTextures(1, &obj);
    obj = 0;
  }
}

REQUEST_GL_EXTENSION( GL_ARB_texture_non_power_of_two );

void CachedTexture::reload()
{
  //..... load file ......
  GLenum internal_format = GL_RGB;
  GLenum data_format = GL_RGB;
  unsigned char *data = NULL;
  if (filename.size() >= 4 && filename.substr(filename.size()-4,4) == ".png")
  {
    //Load a png file, as per the libpng docs:
    FILE *fp = fopen(filename.c_str(), "rb");
    if (!fp)
    {
      cerr << "  cannot open file." << endl;
      return;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, (png_error_ptr)NULL, (png_error_ptr)NULL);
    if (!png)
    {
      cerr << "  cannot alloc read struct." << endl;
      fclose(fp);
      return;
    }
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
      cerr << "  cannot alloc info struct." << endl;
      png_destroy_read_struct(&png, (png_infopp)NULL, (png_infopp)NULL);
      fclose(fp);
      return;
    }
    png_bytep *row_pointers = NULL;
    if (setjmp(png_jmpbuf(png)))
    {
      cerr << "  png interal error." << endl;
      png_destroy_read_struct(&png, &info, (png_infopp)NULL);
      if (data != NULL) delete[] data;
      if (row_pointers != NULL) delete[] row_pointers;
      fclose(fp);
      return;
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    w = png_get_image_width(png, info);
    h = png_get_image_height(png, info);
    {
      //find power-of-two dimensions...
      tw = 1;
      while (tw < w) tw *= 2;
      th = 1;
      while (th < h) th *= 2;
    }
    if (!pad && (tw != w || th != h))
    {
      if (!have_ARB_texture_non_power_of_two())
      {
        cerr << "WARNING: trying to load texture of size " << w << " x " << h << ", but non-power-of-two textures not supported." << endl;
      }
      tw = w;
      th = h;
    }
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE)
      png_set_palette_to_rgb(png);
    if (png_get_bit_depth(png, info) < 8)
      png_set_packing(png);
    if (png_get_bit_depth(png,info) == 16)
      png_set_strip_16(png);
    png_read_update_info(png, info);
    unsigned int rowbytes = png_get_rowbytes(png, info);
    //Make sure it's the format we think it is...
    bool problem = false;
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY)
    {
      data_format = internal_format = GL_LUMINANCE;
      if (rowbytes != w*1) problem = true;
    }
    else if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
      data_format = internal_format = GL_LUMINANCE_ALPHA;
      if (rowbytes != w*2) problem = true;
    }
    else if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE || png_get_color_type(png, info) == PNG_COLOR_TYPE_RGB)
    {
      data_format = internal_format = GL_RGB;
      if (rowbytes != w*3) problem = true;
    }
    else if (png_get_color_type(png, info) == PNG_COLOR_TYPE_RGB_ALPHA)
    {
      data_format = internal_format = GL_RGBA;
      if (rowbytes != w*4) problem = true;
    }
    else
    {
      cerr << "  unknown color format." << endl;
      problem = true;
    }
    if (problem)
    {
      cerr << "  color format problem. (rowbytes: " << rowbytes << " w:" << w << ")" << endl;
      png_destroy_read_struct(&png, &info, NULL);
      fclose(fp);
      return;
    }

    unsigned int pixelbytes = rowbytes / w;
    assert(rowbytes == pixelbytes * w); //sanity check, should have bailed earlier if this was the case.
    data = new uint8_t[th*tw*pixelbytes];
    row_pointers = new png_bytep[th];
    if (flip)
    {
      //texture origin goes top-left, as in most images:
      for (unsigned int r = 0; r < th; ++r)
      {
        row_pointers[r] = (png_bytep)(data + r*pixelbytes*tw);
      }
    }
    else
    {
      //texture origin goes bottom-left, as GL says:
      for (unsigned int r = 0; r < th; ++r)
      {
        row_pointers[th-1-r] = (png_bytep)(data + r*tw*pixelbytes);
      }
    }
    png_read_image(png, row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    delete[] row_pointers;

    if (pad && h != 0 && w != 0)
    {
      //duplicate the last column accross the texture:
      for (unsigned int r = 0; r < h; ++r)
      {
        for (unsigned int c = w; c < tw; ++c)
        {
          memcpy(&(data[(r*tw+c)*pixelbytes]), &(data[(r*tw+w-1)*pixelbytes]),pixelbytes);
        }
      }
      //duplicate the last row down the texture:
      for (unsigned int r = h; r < th; ++r)
      {
        memcpy(&(data[r*pixelbytes*tw]), &(data[(h-1)*pixelbytes*tw]), pixelbytes*tw);
      }
    }
  }
  else
  {
    //well, not a supported image type I'm afraid.
    cerr << "  unknown image type." << endl;
    return;
  }

  glGenTextures(1, &obj);
  glBindTexture(GL_TEXTURE_2D, obj);
  glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, tw, th, 0, data_format, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPopClientAttrib();
  delete[] data;

  loaded = true;
  gl_errors("CachedTexture::reload()");

}

CacheRef< CachedTexture > get_texture(string const &filename, bool pad, bool flip)
{
  string key = "texture:";
  key += (pad?'1':'0');
  key += (flip?'1':'0');
  key += filename;
  CacheRef< CachedTexture > ret(NULL);
  CachedTexture *item = (CachedTexture *)get_cache().get_item(key);
  if (!item)
  {
    item = new CachedTexture(filename, pad, flip);
    if (!item->loaded)
    {
      cerr << "Texture loading failed for " << filename << "." << endl;
      delete item;
      return ret;
    }
    get_cache().add_item(key, item);
  }
  if (item->id != TEXTURE_ID)
  {
    cerr << "Item with key '" << key << "' isn't a texture." << endl;
    return ret;
  }
  ret.set(item);
  return ret;
}

} //namespace Graphics
