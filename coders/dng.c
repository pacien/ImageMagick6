/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                            DDDD   N   N   GGGG                              %
%                            D   D  NN  N  GS                                 %
%                            D   D  N N N  G  GG                              %
%                            D   D  N  NN  G   G                              %
%                            DDDD   N   N   GGGG                              %
%                                                                             %
%                                                                             %
%                  Read the Digital Negative Image Format                     %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1999                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999-2017 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://www.imagemagick.org/script/license.php                           %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/
/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/constitute.h"
#include "magick/delegate.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/geometry.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/layer.h"
#include "magick/list.h"
#include "magick/log.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/opencl.h"
#include "magick/pixel-accessor.h"
#include "magick/property.h"
#include "magick/quantum-private.h"
#include "magick/resource_.h"
#include "magick/static.h"
#include "magick/string_.h"
#include "magick/module.h"
#include "magick/transform.h"
#include "magick/utility.h"
#include "magick/xml-tree.h"
#if defined(MAGICKCORE_RAW_R_DELEGATE)
#include <libraw.h>
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e a d D N G I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadDNGImage() reads an binary file in the Digital Negative format and
%  returns it.  It allocates the memory necessary for the new Image structure
%  and returns a pointer to the new image.
%
%  The format of the ReadDNGImage method is:
%
%      Image *ReadDNGImage(const ImageInfo *image_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o exception: return any errors or warnings in this structure.
%
*/

#if defined(MAGICKCORE_WINDOWS_SUPPORT) && defined(MAGICKCORE_OPENCL_SUPPORT)
static void InitializeDcrawOpenCL(ExceptionInfo *exception)
{
  MagickCLEnv
    clEnv;

  (void) SetEnvironmentVariable("DCR_CL_PLATFORM",NULL);
  (void) SetEnvironmentVariable("DCR_CL_DEVICE",NULL);
  (void) SetEnvironmentVariable("DCR_CL_DISABLED",NULL);
  clEnv=GetDefaultOpenCLEnv();
  if (InitOpenCLEnv(clEnv,exception) != MagickFalse)
    {
      char
        *name;

      MagickBooleanType
        opencl_disabled;

      GetMagickOpenCLEnvParam(clEnv,MAGICK_OPENCL_ENV_PARAM_OPENCL_DISABLED,
        sizeof(MagickBooleanType),&opencl_disabled,exception);
      if (opencl_disabled != MagickFalse)
        {
          (void)SetEnvironmentVariable("DCR_CL_DISABLED","1");
          return;
        }
      GetMagickOpenCLEnvParam(clEnv,MAGICK_OPENCL_ENV_PARAM_PLATFORM_VENDOR,
        sizeof(char *),&name,exception);
      if (name != (char *) NULL)
      {
        (void) SetEnvironmentVariable("DCR_CL_PLATFORM",name);
        name=RelinquishMagickMemory(name);
      }
      GetMagickOpenCLEnvParam(clEnv,MAGICK_OPENCL_ENV_PARAM_DEVICE_NAME,
        sizeof(char *),&name,exception);
      if (name != (char *) NULL)
      {
        (void) SetEnvironmentVariable("DCR_CL_DEVICE",name);
        name=RelinquishMagickMemory(name);
      }
    }
}
#else
#if !defined(MAGICKCORE_RAW_R_DELEGATE)
static void InitializeDcrawOpenCL(ExceptionInfo *magick_unused(exception))
{
  magick_unreferenced(exception);
#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  (void) SetEnvironmentVariable("DCR_CL_DISABLED","1");
#endif
}
#endif
#endif

#if defined(MAGICKCORE_RAW_R_DELEGATE)
static void SetDNGProperties(Image *image,const libraw_data_t *raw_info)
{
  char
    property[MagickPathExtent],
    timestamp[MagickPathExtent];

  (void) FormatMagickTime(raw_info->other.timestamp,MagickPathExtent,timestamp);
  (void) SetImageProperty(image,"dng:timestamp",timestamp);
  (void) SetImageProperty(image,"dng:camera.make",raw_info->idata.make);
  (void) SetImageProperty(image,"dng:camera.model",raw_info->idata.model);
  if (*raw_info->shootinginfo.BodySerial != '\0')
    (void) SetImageProperty(image,"dng:camera.body.serial",
      raw_info->shootinginfo.BodySerial);
  if (raw_info->idata.dng_version != 0)
    {
      (void) FormatLocaleString(property,MagickPathExtent,"%d.%d.%d.%d",
        (raw_info->idata.dng_version >> 24) & 0xff,
        (raw_info->idata.dng_version >> 16) & 0xff,
        (raw_info->idata.dng_version >> 8) & 0xff,
        (raw_info->idata.dng_version >> 0) & 0xff);
      (void) SetImageProperty(image,"dng:version",property);
    }
  if (*raw_info->other.artist != '\0')
    (void) SetImageProperty(image,"dng:artist",raw_info->other.artist);
  (void) FormatLocaleString(property,MagickPathExtent,"%.*gmm",
    GetMagickPrecision(),raw_info->lens.MinFocal);
  (void) SetImageProperty(image,"exif:min.focal",property);
  (void) FormatLocaleString(property,MagickPathExtent,"%.*gmm",
    GetMagickPrecision(),raw_info->lens.MaxFocal);
  (void) SetImageProperty(image,"exif:max.focal",property);
  (void) FormatLocaleString(property,MagickPathExtent,"f/%.*g",
    GetMagickPrecision(),raw_info->lens.MaxAp4MinFocal);
  (void) SetImageProperty(image,"exif:max.aperture.min.focal",property);
  (void) FormatLocaleString(property,MagickPathExtent,"f/%.*g",
    GetMagickPrecision(),raw_info->lens.MaxAp4MaxFocal);
  (void) SetImageProperty(image,"exif:max.aperture.max.focal",property);
  (void) FormatLocaleString(property,MagickPathExtent,"f/%.*g",
    GetMagickPrecision(),raw_info->lens.EXIF_MaxAp);
  (void) SetImageProperty(image,"exif:max.aperture",property);
  (void) FormatLocaleString(property,MagickPathExtent,"%dmm",
    raw_info->lens.FocalLengthIn35mmFormat);
  (void) SetImageProperty(image,"exif:focal.length.in.35mmFormat",property);
  if (*raw_info->lens.LensMake != '\0')
    (void) SetImageProperty(image,"exif:lens.make",raw_info->lens.LensMake);
  if (*raw_info->lens.Lens != '\0')
    (void) SetImageProperty(image,"exif:lens",raw_info->lens.Lens);
}
#endif

static Image *ReadDNGImage(const ImageInfo *image_info,ExceptionInfo *exception)
{
  Image
    *image;

  MagickBooleanType
    status;

  /*
    Open image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  if (image_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      image_info->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  image=AcquireImage(image_info);
  status=OpenBlob(image_info,image,ReadBinaryBlobMode,exception);
  if (status == MagickFalse)
    {
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  (void) CloseBlob(image);
#if defined(MAGICKCORE_RAW_R_DELEGATE)
  {
    int
      errcode;

    libraw_data_t
      *raw_info;

    libraw_processed_image_t
      *raw_image;

    register ssize_t
      y;

    unsigned short
      *p;

    errcode=0;
    raw_info=libraw_init(0);
    if (raw_info == (libraw_data_t *) NULL)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),CoderError,
          libraw_strerror(errcode),"`%s'",image->filename);
        return(DestroyImageList(image));
      }
#if defined(MAGICKCORE_WINDOWS_SUPPORT) && defined(_MSC_VER) && (_MSC_VER > 1310)
    {
      wchar_t
        fileName[MagickPathExtent];

      MultiByteToWideChar(CP_UTF8,0,image->filename,-1,fileName,
        MagickPathExtent);
      errcode=libraw_open_wfile(raw_info,fileName);
    }
#else
    errcode=libraw_open_file(raw_info,image->filename);
#endif
    if (errcode != LIBRAW_SUCCESS)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),CoderError,
          libraw_strerror(errcode),"`%s'",image->filename);
        return(DestroyImageList(image));
      }
    errcode=libraw_unpack(raw_info);
    if (errcode != LIBRAW_SUCCESS)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),CoderError,
          libraw_strerror(errcode),"`%s'",image->filename);
        libraw_close(raw_info);
        return(DestroyImageList(image));
      }
    raw_info->params.output_bps=16;
    errcode=libraw_dcraw_process(raw_info);
    if (errcode != LIBRAW_SUCCESS)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),CoderError,
          libraw_strerror(errcode),"`%s'",image->filename);
        libraw_close(raw_info);
        return(DestroyImageList(image));
      }
    raw_image=libraw_dcraw_make_mem_image(raw_info,&errcode);
    if ((errcode != LIBRAW_SUCCESS) || 
        (raw_image == (libraw_processed_image_t *) NULL) ||
        (raw_image->type != LIBRAW_IMAGE_BITMAP) || (raw_image->bits != 16) ||
        (raw_image->colors < 3) || (raw_image->colors > 4))
      {
        if (raw_image != (libraw_processed_image_t *) NULL)
          libraw_dcraw_clear_mem(raw_image);
        (void) ThrowMagickException(exception,GetMagickModule(),CoderError,
          libraw_strerror(errcode),"`%s'",image->filename);
        libraw_close(raw_info);
        return(DestroyImageList(image));
      }
    image->columns=raw_image->width;
    image->rows=raw_image->height;
    image->depth=16;
    status=SetImageExtent(image,image->columns,image->rows);
    if (status == MagickFalse)
      {
        libraw_dcraw_clear_mem(raw_image);
        libraw_close(raw_info);
        return(DestroyImageList(image));
      }
    if (image_info->ping != MagickFalse)
      {
        libraw_dcraw_clear_mem(raw_image);
        libraw_close(raw_info);
        return(image);
      }
    p=(unsigned short *) raw_image->data;
    for (y=0; y < (ssize_t) image->rows; y++)
    {
      register PixelPacket
        *q;

      register ssize_t
        x;

      q=QueueAuthenticPixels(image,0,y,image->columns,1,exception);
      if (q == (PixelPacket *) NULL)
        break;
      for (x=0; x < (ssize_t) image->columns; x++)
      {
        SetPixelRed(q,ScaleShortToQuantum(*p++));
        SetPixelGreen(q,ScaleShortToQuantum(*p++));
        SetPixelBlue(q,ScaleShortToQuantum(*p++));
        if (raw_image->colors > 3)
          SetPixelAlpha(q,ScaleShortToQuantum(*p++));
        q++;
      }
      if (SyncAuthenticPixels(image,exception) == MagickFalse)
        break;
      if (image->previous == (Image *) NULL)
        {
          status=SetImageProgress(image,LoadImageTag,(MagickOffsetType) y,
            image->rows);
          if (status == MagickFalse)
            break;
        }
    }
    libraw_dcraw_clear_mem(raw_image);
    SetDNGProperties(image,raw_info);
    libraw_close(raw_info);
    return(image);
  }
#else
  {
    ExceptionInfo
      *sans_exception;

    ImageInfo
      *read_info;

    /*
      Convert DNG to PPM with delegate.
    */
    (void) DestroyImageList(image);
    InitializeDcrawOpenCL(exception);
    image=AcquireImage(image_info);
    read_info=CloneImageInfo(image_info);
    SetImageInfoBlob(read_info,(void *) NULL,0);
    (void) InvokeDelegate(read_info,image,"dng:decode",(char *) NULL,exception);
    image=DestroyImage(image);
    (void) FormatLocaleString(read_info->filename,MaxTextExtent,"%s.png",
      read_info->unique);
    sans_exception=AcquireExceptionInfo();
    image=ReadImage(read_info,sans_exception);
    sans_exception=DestroyExceptionInfo(sans_exception);
    if (image == (Image *) NULL)
      {
        (void) FormatLocaleString(read_info->filename,MaxTextExtent,"%s.ppm",
          read_info->unique);
        image=ReadImage(read_info,exception);
      }
    (void) RelinquishUniqueFileResource(read_info->filename);
    if (image != (Image *) NULL)
      {
        char
          filename[MaxTextExtent],
          *xml;

        ExceptionInfo
          *sans;

        (void) CopyMagickString(image->magick,read_info->magick,MaxTextExtent);
        (void) FormatLocaleString(filename,MaxTextExtent,"%s.ufraw",
          read_info->unique);
        sans=AcquireExceptionInfo();
        xml=FileToString(filename,MaxTextExtent,sans);
        (void) RelinquishUniqueFileResource(filename);
        if (xml != (char *) NULL)
          {
            XMLTreeInfo
              *ufraw;

            /*
              Inject.
            */
            ufraw=NewXMLTree(xml,sans);
            if (ufraw != (XMLTreeInfo *) NULL)
              {
                char
                  *content,
                  property[MaxTextExtent];

                const char
                  *tag;

                XMLTreeInfo
                  *next;

                if (image->properties == (void *) NULL)
                  ((Image *) image)->properties=NewSplayTree(
                    CompareSplayTreeString,RelinquishMagickMemory,
                    RelinquishMagickMemory);
                next=GetXMLTreeChild(ufraw,(const char *) NULL);
                while (next != (XMLTreeInfo *) NULL)
                {
                  tag=GetXMLTreeTag(next);
                  if (tag == (char *) NULL)
                    tag="unknown";
                  (void) FormatLocaleString(property,MaxTextExtent,"dng:%s",
                    tag);
                  content=ConstantString(GetXMLTreeContent(next));
                  StripString(content);
                  if ((LocaleCompare(tag,"log") != 0) &&
                      (LocaleCompare(tag,"InputFilename") != 0) &&
                      (LocaleCompare(tag,"OutputFilename") != 0) &&
                      (LocaleCompare(tag,"OutputType") != 0) &&
                      (strlen(content) != 0))
                    (void) AddValueToSplayTree((SplayTreeInfo *)
                      ((Image *) image)->properties,ConstantString(property),
                      content);
                  next=GetXMLTreeSibling(next);
                }
                ufraw=DestroyXMLTree(ufraw);
              }
            xml=DestroyString(xml);
          }
        sans=DestroyExceptionInfo(sans);
      }
    read_info=DestroyImageInfo(read_info);
    return(image);
  }
#endif
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e g i s t e r D N G I m a g e                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterDNGImage() adds attributes for the DNG image format to
%  the list of supported formats.  The attributes include the image format
%  tag, a method to read and/or write the format, whether the format
%  supports the saving of more than one frame to the same file or blob,
%  whether the format supports native in-memory I/O, and a brief
%  description of the format.
%
%  The format of the RegisterDNGImage method is:
%
%      size_t RegisterDNGImage(void)
%
*/
ModuleExport size_t RegisterDNGImage(void)
{
  MagickInfo
    *entry;

  entry=SetMagickInfo("3FR");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Hasselblad CFV/H3D39II");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("ARW");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Sony Alpha Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("DNG");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Digital Negative");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("CR2");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Canon Digital Camera Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("CRW");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Canon Digital Camera Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("DCR");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Kodak Digital Camera Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("ERF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Epson Raw Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("IIQ");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Phase One Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("KDC");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Kodak Digital Camera Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("K25");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Kodak Digital Camera Raw Image Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("MEF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Mamiya Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("MRW");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Sony (Minolta) Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("NEF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Nikon Digital SLR Camera Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("NRW");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Nikon Digital SLR Camera Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("ORF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Olympus Digital Camera Raw Image File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("PEF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Pentax Electronic File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("RAF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Fuji CCD-RAW Graphic File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("RAW");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Raw");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("RMF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Raw Media Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("RW2");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Panasonic Lumix Raw Image");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("SRF");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Sony Raw Format");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("SR2");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Sony Raw Format 2");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("X3F");
  entry->decoder=(DecodeImageHandler *) ReadDNGImage;
  entry->seekable_stream=MagickTrue;
  entry->blob_support=MagickFalse;
  entry->format_type=ExplicitFormatType;
  entry->description=ConstantString("Sigma Camera RAW Picture File");
  entry->module=ConstantString("DNG");
  (void) RegisterMagickInfo(entry);
  return(MagickImageCoderSignature);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   U n r e g i s t e r D N G I m a g e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterDNGImage() removes format registrations made by the
%  BIM module from the list of supported formats.
%
%  The format of the UnregisterBIMImage method is:
%
%      UnregisterDNGImage(void)
%
*/
ModuleExport void UnregisterDNGImage(void)
{
  (void) UnregisterMagickInfo("X3F");
  (void) UnregisterMagickInfo("SR2");
  (void) UnregisterMagickInfo("SRF");
  (void) UnregisterMagickInfo("RW2");
  (void) UnregisterMagickInfo("RMF");
  (void) UnregisterMagickInfo("RAW");
  (void) UnregisterMagickInfo("RAF");
  (void) UnregisterMagickInfo("PEF");
  (void) UnregisterMagickInfo("ORF");
  (void) UnregisterMagickInfo("NRW");
  (void) UnregisterMagickInfo("NEF");
  (void) UnregisterMagickInfo("MRW");
  (void) UnregisterMagickInfo("MEF");
  (void) UnregisterMagickInfo("K25");
  (void) UnregisterMagickInfo("KDC");
  (void) UnregisterMagickInfo("IIQ");
  (void) UnregisterMagickInfo("ERF");
  (void) UnregisterMagickInfo("DCR");
  (void) UnregisterMagickInfo("CRW");
  (void) UnregisterMagickInfo("CR2");
  (void) UnregisterMagickInfo("DNG");
  (void) UnregisterMagickInfo("ARW");
  (void) UnregisterMagickInfo("3FR");
}
