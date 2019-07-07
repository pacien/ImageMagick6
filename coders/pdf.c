/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                            PPPP   DDDD   FFFFF                              %
%                            P   P  D   D  F                                  %
%                            PPPP   D   D  FFF                                %
%                            P      D   D  F                                  %
%                            P      DDDD   F                                  %
%                                                                             %
%                                                                             %
%                   Read/Write Portable Document Format                       %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1992                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999-2019 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://imagemagick.org/script/license.php                               %
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
#include "magick/artifact.h"
#include "magick/attribute.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/cache.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/compress.h"
#include "magick/constitute.h"
#include "magick/delegate.h"
#include "magick/delegate-private.h"
#include "magick/draw.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/geometry.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/list.h"
#include "magick/magick.h"
#include "magick/magick-type.h"
#include "magick/memory_.h"
#include "magick/module.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/nt-base-private.h"
#include "magick/option.h"
#include "magick/pixel-accessor.h"
#include "magick/profile.h"
#include "magick/property.h"
#include "magick/quantum-private.h"
#include "magick/resize.h"
#include "magick/resource_.h"
#include "magick/signature.h"
#include "magick/static.h"
#include "magick/string_.h"
#include "magick/timer-private.h"
#include "magick/token.h"
#include "magick/transform.h"
#include "magick/utility.h"

/*
  Define declarations.
*/
#if defined(MAGICKCORE_TIFF_DELEGATE)
#define CCITTParam  "-1"
#else
#define CCITTParam  "0"
#endif

/*
  Typedef declaractions.
*/
typedef struct _PDFInfo
{
  double
    angle;

  MagickBooleanType
    cmyk,
    cropbox,
    trimbox;

  SegmentInfo
    bounds;

  StringInfo
    *profile;

} PDFInfo;

typedef struct _PDFBuffer
{
  Image
    *image;

  ssize_t
    offset,
    count;

  unsigned char
    data[8192];
} PDFBuffer;

/*
  Forward declarations.
*/
static MagickBooleanType
  WritePDFImage(const ImageInfo *,Image *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I n v o k e P D F D e l e g a t e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  InvokePDFDelegate() executes the PDF interpreter with the specified command.
%
%  The format of the InvokePDFDelegate method is:
%
%      MagickBooleanType InvokePDFDelegate(const MagickBooleanType verbose,
%        const char *command,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o verbose: A value other than zero displays the command prior to
%      executing it.
%
%    o command: the address of a character string containing the command to
%      execute.
%
%    o exception: return any errors or warnings in this structure.
%
*/
#if defined(MAGICKCORE_GS_DELEGATE) || defined(MAGICKCORE_WINDOWS_SUPPORT)
static int MagickDLLCall PDFDelegateMessage(void *handle,const char *message,
  int length)
{
  char
    **messages;

  ssize_t
    offset;

  offset=0;
  messages=(char **) handle;
  if (*messages == (char *) NULL)
    *messages=(char *) AcquireQuantumMemory(length+1,sizeof(char *));
  else
    {
      offset=strlen(*messages);
      *messages=(char *) ResizeQuantumMemory(*messages,offset+length+1,
        sizeof(char *));
    }
  if (*messages == (char *) NULL)
    return(0);
  (void) memcpy(*messages+offset,message,length);
  (*messages)[length+offset] ='\0';
  return(length);
}
#endif

static MagickBooleanType InvokePDFDelegate(const MagickBooleanType verbose,
  const char *command,char *message,ExceptionInfo *exception)
{
  int
    status;

#define ExecuteGhostscriptCommand(command,status) \
{ \
  status=ExternalDelegateCommand(MagickFalse,verbose,command,message, \
    exception); \
  if (status == 0) \
    return(MagickTrue); \
  if (status < 0) \
    return(MagickFalse); \
  (void) ThrowMagickException(exception,GetMagickModule(),DelegateError, \
    "FailedToExecuteCommand","`%s' (%d)",command,status); \
  return(MagickFalse); \
}

#if defined(MAGICKCORE_GS_DELEGATE) || defined(MAGICKCORE_WINDOWS_SUPPORT)
#define SetArgsStart(command,args_start) \
  if (args_start == (const char *) NULL) \
    { \
      if (*command != '"') \
        args_start=strchr(command,' '); \
      else \
        { \
          args_start=strchr(command+1,'"'); \
          if (args_start != (const char *) NULL) \
            args_start++; \
        } \
    }

  char
    **argv,
    *errors;

  const char
    *args_start=NULL;

  const GhostInfo
    *ghost_info;

  gs_main_instance
    *interpreter;

  gsapi_revision_t
    revision;

  int
    argc,
    code;

  register ssize_t
    i;

#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  ghost_info=NTGhostscriptDLLVectors();
#else
  GhostInfo
    ghost_info_struct;

  ghost_info=(&ghost_info_struct);
  (void) memset(&ghost_info_struct,0,sizeof(ghost_info_struct));
  ghost_info_struct.delete_instance=(void (*)(gs_main_instance *))
    gsapi_delete_instance;
  ghost_info_struct.exit=(int (*)(gs_main_instance *)) gsapi_exit;
  ghost_info_struct.new_instance=(int (*)(gs_main_instance **,void *))
    gsapi_new_instance;
  ghost_info_struct.init_with_args=(int (*)(gs_main_instance *,int,char **))
    gsapi_init_with_args;
  ghost_info_struct.run_string=(int (*)(gs_main_instance *,const char *,int,
    int *)) gsapi_run_string;
  ghost_info_struct.set_stdio=(int (*)(gs_main_instance *,int (*)(void *,char *,
    int),int (*)(void *,const char *,int),int (*)(void *, const char *, int)))
    gsapi_set_stdio;
  ghost_info_struct.revision=(int (*)(gsapi_revision_t *,int)) gsapi_revision;
#endif
  if (ghost_info == (GhostInfo *) NULL)
    ExecuteGhostscriptCommand(command,status);
  if ((ghost_info->revision)(&revision,sizeof(revision)) != 0)
    revision.revision=0;
  if (verbose != MagickFalse)
    {
      (void) fprintf(stdout,"[ghostscript library %.2f]",(double)
        revision.revision/100.0);
      SetArgsStart(command,args_start);
      (void) fputs(args_start,stdout);
    }
  interpreter=(gs_main_instance *) NULL;
  errors=(char *) NULL;
  status=(ghost_info->new_instance)(&interpreter,(void *) &errors);
  if (status < 0)
    ExecuteGhostscriptCommand(command,status);
  code=0;
  argv=StringToArgv(command,&argc);
  if (argv == (char **) NULL)
    {
      (ghost_info->delete_instance)(interpreter);
      return(MagickFalse);
    }
  (void) (ghost_info->set_stdio)(interpreter,(int (MagickDLLCall *)(void *,
    char *,int)) NULL,PDFDelegateMessage,PDFDelegateMessage);
  status=(ghost_info->init_with_args)(interpreter,argc-1,argv+1);
  if (status == 0)
    status=(ghost_info->run_string)(interpreter,"systemdict /start get exec\n",
      0,&code);
  (ghost_info->exit)(interpreter);
  (ghost_info->delete_instance)(interpreter);
  for (i=0; i < (ssize_t) argc; i++)
    argv[i]=DestroyString(argv[i]);
  argv=(char **) RelinquishMagickMemory(argv);
  if (status != 0)
    {
      SetArgsStart(command,args_start);
      if (status == -101) /* quit */
        (void) FormatLocaleString(message,MaxTextExtent,
          "[ghostscript library %.2f]%s: %s",(double) revision.revision/100.0,
          args_start,errors);
      else
        {
          (void) ThrowMagickException(exception,GetMagickModule(),DelegateError,
            "PDFDelegateFailed","`[ghostscript library %.2f]%s': %s",(double)
            revision.revision/100.0,args_start,errors);
          if (errors != (char *) NULL)
            errors=DestroyString(errors);
          (void) LogMagickEvent(CoderEvent,GetMagickModule(),
            "Ghostscript returns status %d, exit code %d",status,code);
          return(MagickFalse);
        }
    }
  if (errors != (char *) NULL)
    errors=DestroyString(errors);
  return(MagickTrue);
#else
  ExecuteGhostscriptCommand(command,status);
#endif
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I s P D F                                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsPDF() returns MagickTrue if the image format type, identified by the
%  magick string, is PDF.
%
%  The format of the IsPDF method is:
%
%      MagickBooleanType IsPDF(const unsigned char *magick,const size_t offset)
%
%  A description of each parameter follows:
%
%    o magick: compare image format pattern against these bytes.
%
%    o offset: Specifies the offset of the magick string.
%
*/
static MagickBooleanType IsPDF(const unsigned char *magick,const size_t offset)
{
  if (offset < 5)
    return(MagickFalse);
  if (LocaleNCompare((const char *) magick,"%PDF-",5) == 0)
    return(MagickTrue);
  return(MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e a d P D F I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadPDFImage() reads a Portable Document Format image file and
%  returns it.  It allocates the memory necessary for the new Image structure
%  and returns a pointer to the new image.
%
%  The format of the ReadPDFImage method is:
%
%      Image *ReadPDFImage(const ImageInfo *image_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static MagickBooleanType IsPDFRendered(const char *path)
{
  MagickBooleanType
    status;

  struct stat
    attributes;

  if ((path == (const char *) NULL) || (*path == '\0'))
    return(MagickFalse);
  status=GetPathAttributes(path,&attributes);
  if ((status != MagickFalse) && S_ISREG(attributes.st_mode) &&
      (attributes.st_size > 0))
    return(MagickTrue);
  return(MagickFalse);
}

static inline int ReadPDFByte(PDFBuffer *buffer)
{
  if ((buffer->offset == buffer->count) && (buffer->offset > 0))
    {
      if (buffer->count != (ssize_t) sizeof(buffer->data))
        return(EOF);
      buffer->offset=0;
    }
  if (buffer->offset == 0)
    {
      buffer->count=ReadBlob(buffer->image,sizeof(buffer->data),buffer->data);
      if (buffer->count < 1)
        return(EOF);
    }
  return(buffer->data[buffer->offset++]);
}

static char *MovePDFBuffer(PDFBuffer *buffer)
{
  ssize_t
    i;

  i=1; /* Skip first to avoid reload of buffer; */
  while (buffer->offset < buffer->count)
    buffer->data[i++]=buffer->data[buffer->offset++];
  buffer->count=ReadBlob(buffer->image,sizeof(buffer->data)-i,buffer->data+i);
  buffer->count+=i;
  buffer->offset=1;
  return((char *) buffer->data+1);
}

static inline void CheckRemainingPDFBuffer(PDFBuffer *buffer,size_t length)
{
  if ((buffer->offset+length) > (ssize_t) sizeof(buffer->data))
    (void) MovePDFBuffer(buffer);
}

static inline void SkipPDFBytes(PDFBuffer *buffer,size_t length)
{
  CheckRemainingPDFBuffer(buffer,length);
  if ((buffer->offset+length) < buffer->count)
    buffer->offset+=length;
}

static inline MagickBooleanType ComparePDFBuffer(const char *p,
  PDFBuffer *buffer,const size_t length)
{
  const char
    *q;

  CheckRemainingPDFBuffer(buffer,length);
  q=(const char *) buffer->data+buffer->offset;
  if (LocaleNCompare(p,q,length) != 0)
    return(MagickFalse);
  return(MagickTrue);
}

static void ReadPDFXMPProfile(PDFInfo *pdf_info,PDFBuffer *buffer)
{
#define BeginXMPPacket  "?xpacket begin="
#define EndXMPPacket  "<?xpacket end="

  int
    c;

  MagickBooleanType
    found_end;

  register char
    *p;

  size_t
    length;

  ssize_t
    count;

  if (pdf_info->profile != (StringInfo *) NULL)
    return;
  if (ComparePDFBuffer(BeginXMPPacket,buffer,strlen(BeginXMPPacket)) == MagickFalse)
    return;
  length=8192;
  pdf_info->profile=AcquireStringInfo(length);
  found_end=MagickFalse;
  p=(char *) GetStringInfoDatum(pdf_info->profile);
  *p++='<';
  count=1;
  for (c=ReadPDFByte(buffer); c != EOF; c=ReadPDFByte(buffer))
  {
    if (count == (ssize_t) length)
      {
        length+=length;
        SetStringInfoLength(pdf_info->profile,length);
        p=(char *) GetStringInfoDatum(pdf_info->profile)+count;
      }
    count++;
    *p++=(char) c;
    if (found_end == MagickFalse)
      found_end=ComparePDFBuffer(EndXMPPacket,buffer,strlen(EndXMPPacket));
    else
      {
        if (c == (int) '>')
          break;
      }
  }
  SetStringInfoLength(pdf_info->profile,(size_t) count);
}

static void ReadPDFInfo(const ImageInfo *image_info,Image *image,
  PDFInfo *pdf_info)
{
#define CMYKProcessColor  "CMYKProcessColor"
#define CropBox  "CropBox"
#define DefaultCMYK  "DefaultCMYK"
#define DeviceCMYK  "DeviceCMYK"
#define MediaBox  "MediaBox"
#define PDFRotate  "Rotate"
#define SpotColor  "Separation"
#define TrimBox  "TrimBox"
#define PDFVersion  "PDF-"

  char
    version[MagickPathExtent];

  int
    c;

  PDFBuffer
    buffer;

  register char
    *p;

  register ssize_t
    i;

  SegmentInfo
    bounds;

  size_t
    spotcolor;

  ssize_t
    count;

  (void) memset(&bounds,0,sizeof(bounds));
  (void) memset(pdf_info,0,sizeof(*pdf_info));
  pdf_info->cmyk=image_info->colorspace == CMYKColorspace ? MagickTrue :
    MagickFalse;
  pdf_info->cropbox=IsStringTrue(GetImageOption(image_info,"pdf:use-cropbox"));
  pdf_info->trimbox=IsStringTrue(GetImageOption(image_info,"pdf:use-trimbox"));
  *version='\0';
  spotcolor=0;
  (void) memset(&buffer,0,sizeof(buffer));
  buffer.image=image;
  for (c=ReadPDFByte(&buffer); c != EOF; c=ReadPDFByte(&buffer))
  {
    switch(c)
    {
      case '%':
      {
        if (*version == '\0')
          {
            i=0;
            for (c=ReadPDFByte(&buffer); c != EOF; c=ReadPDFByte(&buffer))
            {
              if ((c == '\r') || (c == '\n') || ((i+1) == MagickPathExtent))
                break;
              version[i++]=(char) c;
            }
            version[i]='\0';
          }
        continue;
      }
      case '<':
      {
        ReadPDFXMPProfile(pdf_info,&buffer);
        continue;
      }
      case '/':
        break;
      default:
        continue;
    }
    if (ComparePDFBuffer(PDFRotate,&buffer,strlen(PDFRotate)) != MagickFalse)
      {
        p=MovePDFBuffer(&buffer);
        (void) sscanf(p,PDFRotate" %lf",&pdf_info->angle);
      }
    if (pdf_info->cmyk == MagickFalse)
      {
        if ((ComparePDFBuffer(DefaultCMYK,&buffer,strlen(DefaultCMYK)) != MagickFalse) ||
            (ComparePDFBuffer(DeviceCMYK,&buffer,strlen(DeviceCMYK)) != MagickFalse) ||
            (ComparePDFBuffer(CMYKProcessColor,&buffer,strlen(CMYKProcessColor)) != MagickFalse))
          {
            pdf_info->cmyk=MagickTrue;
            continue;
          }
      }
    if (ComparePDFBuffer(SpotColor,&buffer,strlen(SpotColor)) != MagickFalse)
      {
        char
          name[MagickPathExtent],
          property[MagickPathExtent],
          *value;

        /*
          Note spot names.
        */
        (void) FormatLocaleString(property,MagickPathExtent,
          "pdf:SpotColor-%.20g",(double) spotcolor++);
        i=0;
        SkipPDFBytes(&buffer,strlen(SpotColor)+1);
        for (c=ReadPDFByte(&buffer); c != EOF; c=ReadPDFByte(&buffer))
        {
          if ((isspace(c) != 0) || (c == '/') || ((i+1) == MagickPathExtent))
            break;
          name[i++]=(char) c;
        }
        name[i]='\0';
        value=ConstantString(name);
        (void) SubstituteString(&value,"#20"," ");
        if (*value != '\0')
          (void) SetImageProperty(image,property,value);
        value=DestroyString(value);
        continue;
      }
    if (image_info->page != (char *) NULL)
      continue;
    count=0;
    if (pdf_info->cropbox != MagickFalse)
      {
        if (ComparePDFBuffer(CropBox,&buffer,strlen(CropBox)) != MagickFalse)
          {
            /*
              Note region defined by crop box.
            */
            p=MovePDFBuffer(&buffer);
            count=(ssize_t) sscanf(p,"CropBox [%lf %lf %lf %lf",&bounds.x1,
              &bounds.y1,&bounds.x2,&bounds.y2);
            if (count != 4)
              count=(ssize_t) sscanf(p,"CropBox[%lf %lf %lf %lf",&bounds.x1,
                &bounds.y1,&bounds.x2,&bounds.y2);
          }
      }
    else
      if (pdf_info->trimbox != MagickFalse)
        {
          if (ComparePDFBuffer(TrimBox,&buffer,strlen(TrimBox)) != MagickFalse)
            {
              /*
                Note region defined by trim box.
              */
              p=MovePDFBuffer(&buffer);
              count=(ssize_t) sscanf(p,"TrimBox [%lf %lf %lf %lf",&bounds.x1,
                &bounds.y1,&bounds.x2,&bounds.y2);
              if (count != 4)
                count=(ssize_t) sscanf(p,"TrimBox[%lf %lf %lf %lf",&bounds.x1,
                  &bounds.y1,&bounds.x2,&bounds.y2);
            }
        }
      else
        if (ComparePDFBuffer(MediaBox,&buffer,strlen(MediaBox)) != MagickFalse)
          {
            /*
              Note region defined by media box.
            */
            p=MovePDFBuffer(&buffer);
            count=(ssize_t) sscanf(p,"MediaBox [%lf %lf %lf %lf",&bounds.x1,
              &bounds.y1,&bounds.x2,&bounds.y2);
            if (count != 4)
              count=(ssize_t) sscanf(p,"MediaBox[%lf %lf %lf %lf",&bounds.x1,
                &bounds.y1,&bounds.x2,&bounds.y2);
          }
    if (count != 4)
      continue;
    if ((fabs(bounds.x2-bounds.x1) <= fabs(pdf_info->bounds.x2-pdf_info->bounds.x1)) ||
        (fabs(bounds.y2-bounds.y1) <= fabs(pdf_info->bounds.y2-pdf_info->bounds.y1)))
      continue;
    pdf_info->bounds=bounds;
  }
  if (version[0] != '\0')
    (void) SetImageProperty(image,"pdf:Version",version);
}

static inline void CleanupPDFInfo(PDFInfo *pdf_info)
{
  if (pdf_info->profile != (StringInfo *) NULL)
    pdf_info->profile=DestroyStringInfo(pdf_info->profile);
}

static Image *ReadPDFImage(const ImageInfo *image_info,ExceptionInfo *exception)
{
  char
    command[MaxTextExtent],
    *density,
    filename[MaxTextExtent],
    geometry[MaxTextExtent],
    *options,
    input_filename[MaxTextExtent],
    message[MaxTextExtent],
    postscript_filename[MaxTextExtent];

  const char
    *option;

  const DelegateInfo
    *delegate_info;

  GeometryInfo
    geometry_info;

  Image
    *image,
    *next,
    *pdf_image;

  ImageInfo
    *read_info;

  int
    file;

  MagickBooleanType
    fitPage,
    status,
    stop_on_error;

  MagickStatusType
    flags;

  PDFInfo
    info;

  PointInfo
    delta;

  RectangleInfo
    page;

  register ssize_t
    i;

  size_t
    scene;

  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  if (image_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      image_info->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  /*
    Open image file.
  */
  image=AcquireImage(image_info);
  status=OpenBlob(image_info,image,ReadBinaryBlobMode,exception);
  if (status == MagickFalse)
    {
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  status=AcquireUniqueSymbolicLink(image_info->filename,input_filename);
  if (status == MagickFalse)
    {
      ThrowFileException(exception,FileOpenError,"UnableToCreateTemporaryFile",
        image_info->filename);
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  /*
    Set the page density.
  */
  delta.x=DefaultResolution;
  delta.y=DefaultResolution;
  if ((image->x_resolution == 0.0) || (image->y_resolution == 0.0))
    {
      flags=ParseGeometry(PSDensityGeometry,&geometry_info);
      image->x_resolution=geometry_info.rho;
      image->y_resolution=geometry_info.sigma;
      if ((flags & SigmaValue) == 0)
        image->y_resolution=image->x_resolution;
    }
  if (image_info->density != (char *) NULL)
    {
      flags=ParseGeometry(image_info->density,&geometry_info);
      image->x_resolution=geometry_info.rho;
      image->y_resolution=geometry_info.sigma;
      if ((flags & SigmaValue) == 0)
        image->y_resolution=image->x_resolution;
    }
  (void) memset(&page,0,sizeof(page));
  (void) ParseAbsoluteGeometry(PSPageGeometry,&page);
  if (image_info->page != (char *) NULL)
    (void) ParseAbsoluteGeometry(image_info->page,&page);
  page.width=(size_t) ceil((double) (page.width*image->x_resolution/delta.x)-
    0.5);
  page.height=(size_t) ceil((double) (page.height*image->y_resolution/delta.y)-
    0.5);
  /*
    Determine page geometry from the PDF media box.
  */
  ReadPDFInfo(image_info,image,&info);
  (void) CloseBlob(image);
  /*
    Set PDF render geometry.
  */
  if ((fabs(info.bounds.x2-info.bounds.x1) >= MagickEpsilon) &&
      (fabs(info.bounds.y2-info.bounds.y1) >= MagickEpsilon))
    {
      (void) FormatLocaleString(geometry,MaxTextExtent,"%gx%g%+.15g%+.15g",
        info.bounds.x2-info.bounds.x1,info.bounds.y2-
        info.bounds.y1,info.bounds.x1,info.bounds.y1);
      (void) SetImageProperty(image,"pdf:HiResBoundingBox",geometry);
      page.width=(size_t) ceil((double) ((info.bounds.x2-info.bounds.x1)*
        image->x_resolution/delta.x)-0.5);
      page.height=(size_t) ceil((double) ((info.bounds.y2-info.bounds.y1)*
        image->y_resolution/delta.y)-0.5);
    }
  fitPage=MagickFalse;
  option=GetImageOption(image_info,"pdf:fit-page");
  if (option != (char *) NULL)
    {
      char
        *geometry;

      MagickStatusType
        flags;

      geometry=GetPageGeometry(option);
      flags=ParseMetaGeometry(geometry,&page.x,&page.y,&page.width,
        &page.height);
      if (flags == NoValue)
        {
          (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
            "InvalidGeometry","`%s'",option);
          CleanupPDFInfo(&info);
          image=DestroyImage(image);
          return((Image *) NULL);
        }
      page.width=(size_t) ceil((double) (page.width*image->x_resolution/
        delta.x)-0.5);
      page.height=(size_t) ceil((double) (page.height*image->y_resolution/
        delta.y)-0.5);
      geometry=DestroyString(geometry);
      fitPage=MagickTrue;
    }
  if ((fabs(info.angle) == 90.0) || (fabs(info.angle) == 270.0))
    {
      size_t
        swap;

      swap=page.width;
      page.width=page.height;
      page.height=swap;
    }
  if (IssRGBCompatibleColorspace(image_info->colorspace) != MagickFalse)
    info.cmyk=MagickFalse;
  stop_on_error=IsStringTrue(GetImageOption(image_info,"pdf:stop-on-error"));
  /*
    Create Ghostscript control file.
  */
  file=AcquireUniqueFileResource(postscript_filename);
  if (file == -1)
    {
      ThrowFileException(exception,FileOpenError,"UnableToCreateTemporaryFile",
        image_info->filename);
      CleanupPDFInfo(&info);
      image=DestroyImage(image);
      return((Image *) NULL);
    }
  (void) write(file," ",1);
  file=close(file)-1;
  /*
    Render Postscript with the Ghostscript delegate.
  */
  if (image_info->monochrome != MagickFalse)
    delegate_info=GetDelegateInfo("ps:mono",(char *) NULL,exception);
  else
     if (info.cmyk != MagickFalse)
       delegate_info=GetDelegateInfo("ps:cmyk",(char *) NULL,exception);
     else
       delegate_info=GetDelegateInfo("ps:alpha",(char *) NULL,exception);
  if (delegate_info == (const DelegateInfo *) NULL)
    {
      (void) RelinquishUniqueFileResource(postscript_filename);
      CleanupPDFInfo(&info);
      image=DestroyImage(image);
      return((Image *) NULL);
    }
  density=AcquireString("");
  options=AcquireString("");
  (void) FormatLocaleString(density,MaxTextExtent,"%gx%g",image->x_resolution,
    image->y_resolution);
  if ((image_info->page != (char *) NULL) || (fitPage != MagickFalse))
    (void) FormatLocaleString(options,MaxTextExtent,"-g%.20gx%.20g ",(double)
      page.width,(double) page.height);
  if (fitPage != MagickFalse)
    (void) ConcatenateMagickString(options,"-dPSFitPage ",MaxTextExtent);
  if (info.cmyk != MagickFalse)
    (void) ConcatenateMagickString(options,"-dUseCIEColor ",MaxTextExtent);
  if (info.cropbox != MagickFalse)
    (void) ConcatenateMagickString(options,"-dUseCropBox ",MaxTextExtent);
  if (info.trimbox != MagickFalse)
    (void) ConcatenateMagickString(options,"-dUseTrimBox ",MaxTextExtent);
  if (stop_on_error != MagickFalse)
    (void) ConcatenateMagickString(options,"-dPDFSTOPONERROR ",MaxTextExtent);
  option=GetImageOption(image_info,"authenticate");
  if (option != (char *) NULL)
    {
      char
        passphrase[MaxTextExtent];

      (void) FormatLocaleString(passphrase,MaxTextExtent,
        "\"-sPDFPassword=%s\" ",option);
      (void) ConcatenateMagickString(options,passphrase,MaxTextExtent);
    }
  read_info=CloneImageInfo(image_info);
  *read_info->magick='\0';
  if (read_info->number_scenes != 0)
    {
      char
        pages[MaxTextExtent];

      (void) FormatLocaleString(pages,MaxTextExtent,"-dFirstPage=%.20g "
        "-dLastPage=%.20g",(double) read_info->scene+1,(double)
        (read_info->scene+read_info->number_scenes));
      (void) ConcatenateMagickString(options,pages,MaxTextExtent);
      read_info->number_scenes=0;
      if (read_info->scenes != (char *) NULL)
        *read_info->scenes='\0';
    }
  (void) CopyMagickString(filename,read_info->filename,MaxTextExtent);
  (void) AcquireUniqueFilename(filename);
  (void) RelinquishUniqueFileResource(filename);
  (void) ConcatenateMagickString(filename,"%d",MaxTextExtent);
  (void) FormatLocaleString(command,MaxTextExtent,
    GetDelegateCommands(delegate_info),
    read_info->antialias != MagickFalse ? 4 : 1,
    read_info->antialias != MagickFalse ? 4 : 1,density,options,filename,
    postscript_filename,input_filename);
  options=DestroyString(options);
  density=DestroyString(density);
  *message='\0';
  status=InvokePDFDelegate(read_info->verbose,command,message,exception);
  (void) RelinquishUniqueFileResource(postscript_filename);
  (void) RelinquishUniqueFileResource(input_filename);
  pdf_image=(Image *) NULL;
  if (status == MagickFalse)
    for (i=1; ; i++)
    {
      (void) InterpretImageFilename(image_info,image,filename,(int) i,
        read_info->filename);
      if (IsPDFRendered(read_info->filename) == MagickFalse)
        break;
      (void) RelinquishUniqueFileResource(read_info->filename);
    }
  else
    for (i=1; ; i++)
    {
      (void) InterpretImageFilename(image_info,image,filename,(int) i,
        read_info->filename);
      if (IsPDFRendered(read_info->filename) == MagickFalse)
        break;
      read_info->blob=NULL;
      read_info->length=0;
      next=ReadImage(read_info,exception);
      (void) RelinquishUniqueFileResource(read_info->filename);
      if (next == (Image *) NULL)
        break;
      AppendImageToList(&pdf_image,next);
    }
  read_info=DestroyImageInfo(read_info);
  if (pdf_image == (Image *) NULL)
    {
      if (*message != '\0')
        (void) ThrowMagickException(exception,GetMagickModule(),DelegateError,
          "PDFDelegateFailed","`%s'",message);
      CleanupPDFInfo(&info);
      image=DestroyImage(image);
      return((Image *) NULL);
    }
  if (LocaleCompare(pdf_image->magick,"BMP") == 0)
    {
      Image
        *cmyk_image;

      cmyk_image=ConsolidateCMYKImages(pdf_image,exception);
      if (cmyk_image != (Image *) NULL)
        {
          pdf_image=DestroyImageList(pdf_image);
          pdf_image=cmyk_image;
        }
    }
  if (info.profile != (StringInfo *) NULL)
    (void) SetImageProfile(image,"xmp",info.profile);
  CleanupPDFInfo(&info);
  (void) CloseBlob(image);
  if (image_info->number_scenes != 0)
    {
      Image
        *clone_image;

      register ssize_t
        i;

      /*
        Add place holder images to meet the subimage specification requirement.
      */
      for (i=0; i < (ssize_t) image_info->scene; i++)
      {
        clone_image=CloneImage(pdf_image,1,1,MagickTrue,exception);
        if (clone_image != (Image *) NULL)
          PrependImageToList(&pdf_image,clone_image);
      }
    }
  do
  {
    (void) CopyMagickString(pdf_image->filename,filename,MaxTextExtent);
    (void) CopyMagickString(pdf_image->magick,image->magick,MaxTextExtent);
    pdf_image->page=page;
    (void) CloneImageProfiles(pdf_image,image);
    (void) CloneImageProperties(pdf_image,image);
    next=SyncNextImageInList(pdf_image);
    if (next != (Image *) NULL)
      pdf_image=next;
  } while (next != (Image *) NULL);
  image=DestroyImage(image);
  scene=0;
  for (next=GetFirstImageInList(pdf_image); next != (Image *) NULL; )
  {
    next->scene=scene++;
    next=GetNextImageInList(next);
  }
  return(GetFirstImageInList(pdf_image));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e g i s t e r P D F I m a g e                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterPDFImage() adds properties for the PDF image format to
%  the list of supported formats.  The properties include the image format
%  tag, a method to read and/or write the format, whether the format
%  supports the saving of more than one frame to the same file or blob,
%  whether the format supports native in-memory I/O, and a brief
%  description of the format.
%
%  The format of the RegisterPDFImage method is:
%
%      size_t RegisterPDFImage(void)
%
*/
ModuleExport size_t RegisterPDFImage(void)
{
  MagickInfo
    *entry;

  entry=SetMagickInfo("AI");
  entry->decoder=(DecodeImageHandler *) ReadPDFImage;
  entry->encoder=(EncodeImageHandler *) WritePDFImage;
  entry->adjoin=MagickFalse;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->description=ConstantString("Adobe Illustrator CS2");
  entry->mime_type=ConstantString("application/pdf");
  entry->module=ConstantString("PDF");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("EPDF");
  entry->decoder=(DecodeImageHandler *) ReadPDFImage;
  entry->encoder=(EncodeImageHandler *) WritePDFImage;
  entry->adjoin=MagickFalse;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->description=ConstantString("Encapsulated Portable Document Format");
  entry->mime_type=ConstantString("application/pdf");
  entry->module=ConstantString("PDF");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("PDF");
  entry->decoder=(DecodeImageHandler *) ReadPDFImage;
  entry->encoder=(EncodeImageHandler *) WritePDFImage;
  entry->magick=(IsImageFormatHandler *) IsPDF;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->description=ConstantString("Portable Document Format");
  entry->mime_type=ConstantString("application/pdf");
  entry->module=ConstantString("PDF");
  (void) RegisterMagickInfo(entry);
  entry=SetMagickInfo("PDFA");
  entry->decoder=(DecodeImageHandler *) ReadPDFImage;
  entry->encoder=(EncodeImageHandler *) WritePDFImage;
  entry->magick=(IsImageFormatHandler *) IsPDF;
  entry->blob_support=MagickFalse;
  entry->seekable_stream=MagickTrue;
  entry->description=ConstantString("Portable Document Archive Format");
  entry->mime_type=ConstantString("application/pdf");
  entry->module=ConstantString("PDF");
  (void) RegisterMagickInfo(entry);
  return(MagickImageCoderSignature);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   U n r e g i s t e r P D F I m a g e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterPDFImage() removes format registrations made by the
%  PDF module from the list of supported formats.
%
%  The format of the UnregisterPDFImage method is:
%
%      UnregisterPDFImage(void)
%
*/
ModuleExport void UnregisterPDFImage(void)
{
  (void) UnregisterMagickInfo("AI");
  (void) UnregisterMagickInfo("EPDF");
  (void) UnregisterMagickInfo("PDF");
  (void) UnregisterMagickInfo("PDFA");
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   W r i t e P D F I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WritePDFImage() writes an image in the Portable Document image
%  format.
%
%  The format of the WritePDFImage method is:
%
%      MagickBooleanType WritePDFImage(const ImageInfo *image_info,Image *image)
%
%  A description of each parameter follows.
%
%    o image_info: the image info.
%
%    o image:  The image.
%
*/

static char *EscapeParenthesis(const char *source)
{
  char
    *destination;

  register char
    *q;

  register const char
    *p;

  size_t
    length;

  assert(source != (const char *) NULL);
  length=0;
  for (p=source; *p != '\0'; p++)
  {
    if ((*p == '\\') || (*p == '(') || (*p == ')'))
      {
        if (~length < 1)
          ThrowFatalException(ResourceLimitFatalError,"UnableToEscapeString");
        length++;
      }
    length++;
  }
  destination=(char *) NULL;
  if (~length >= (MaxTextExtent-1))
    destination=(char *) AcquireQuantumMemory(length+MaxTextExtent,
      sizeof(*destination));
  if (destination == (char *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"UnableToEscapeString");
  *destination='\0';
  q=destination;
  for (p=source; *p != '\0'; p++)
  {
    if ((*p == '\\') || (*p == '(') || (*p == ')'))
      *q++='\\';
    *q++=(*p);
  }
  *q='\0';
  return(destination);
}

static size_t UTF8ToUTF16(const unsigned char *utf8,wchar_t *utf16)
{
  register const unsigned char
    *p;

  if (utf16 != (wchar_t *) NULL)
    {
      register wchar_t
        *q;

      wchar_t
        c;

      /*
        Convert UTF-8 to UTF-16.
      */
      q=utf16;
      for (p=utf8; *p != '\0'; p++)
      {
        if ((*p & 0x80) == 0)
          *q=(*p);
        else
          if ((*p & 0xE0) == 0xC0)
            {
              c=(*p);
              *q=(c & 0x1F) << 6;
              p++;
              if ((*p & 0xC0) != 0x80)
                return(0);
              *q|=(*p & 0x3F);
            }
          else
            if ((*p & 0xF0) == 0xE0)
              {
                c=(*p);
                *q=c << 12;
                p++;
                if ((*p & 0xC0) != 0x80)
                  return(0);
                c=(*p);
                *q|=(c & 0x3F) << 6;
                p++;
                if ((*p & 0xC0) != 0x80)
                  return(0);
                *q|=(*p & 0x3F);
              }
            else
              return(0);
        q++;
      }
      *q++=(wchar_t) '\0';
      return((size_t) (q-utf16));
    }
  /*
    Compute UTF-16 string length.
  */
  for (p=utf8; *p != '\0'; p++)
  {
    if ((*p & 0x80) == 0)
      ;
    else
      if ((*p & 0xE0) == 0xC0)
        {
          p++;
          if ((*p & 0xC0) != 0x80)
            return(0);
        }
      else
        if ((*p & 0xF0) == 0xE0)
          {
            p++;
            if ((*p & 0xC0) != 0x80)
              return(0);
            p++;
            if ((*p & 0xC0) != 0x80)
              return(0);
         }
       else
         return(0);
  }
  return((size_t) (p-utf8));
}

static wchar_t *ConvertUTF8ToUTF16(const unsigned char *source,size_t *length)
{
  wchar_t
    *utf16;

  *length=UTF8ToUTF16(source,(wchar_t *) NULL);
  if (*length == 0)
    {
      register ssize_t
        i;

      /*
        Not UTF-8, just copy.
      */
      *length=strlen((const char *) source);
      utf16=(wchar_t *) AcquireQuantumMemory(*length+1,sizeof(*utf16));
      if (utf16 == (wchar_t *) NULL)
        return((wchar_t *) NULL);
      for (i=0; i <= (ssize_t) *length; i++)
        utf16[i]=source[i];
      return(utf16);
    }
  utf16=(wchar_t *) AcquireQuantumMemory(*length+1,sizeof(*utf16));
  if (utf16 == (wchar_t *) NULL)
    return((wchar_t *) NULL);
  *length=UTF8ToUTF16(source,utf16);
  return(utf16);
}

static MagickBooleanType Huffman2DEncodeImage(const ImageInfo *image_info,
  Image *image,Image *inject_image)
{
  Image
    *group4_image;

  ImageInfo
    *write_info;

  MagickBooleanType
    status;

  size_t
    length;

  unsigned char
    *group4;

  group4_image=CloneImage(inject_image,0,0,MagickTrue,&image->exception);
  if (group4_image == (Image *) NULL)
    return(MagickFalse);
  status=MagickTrue;
  write_info=CloneImageInfo(image_info);
  (void) CopyMagickString(write_info->filename,"GROUP4:",MaxTextExtent);
  (void) CopyMagickString(write_info->magick,"GROUP4",MaxTextExtent);
  group4=(unsigned char *) ImageToBlob(write_info,group4_image,&length,
    &image->exception);
  group4_image=DestroyImage(group4_image);
  write_info=DestroyImageInfo(write_info);
  if (group4 == (unsigned char *) NULL)
    return(MagickFalse);
  if (WriteBlob(image,length,group4) != (ssize_t) length)
    status=MagickFalse;
  group4=(unsigned char *) RelinquishMagickMemory(group4);
  return(status);
}

static MagickBooleanType WritePDFImage(const ImageInfo *image_info,Image *image)
{
#define CFormat  "/Filter [ /%s ]\n"
#define ObjectsPerImage  14
#define ThrowPDFException(exception,message) \
{ \
  if (xref != (MagickOffsetType *) NULL) \
    xref=(MagickOffsetType *) RelinquishMagickMemory(xref); \
  ThrowWriterException((exception),(message)); \
}

DisableMSCWarning(4310)
  static const char
    XMPProfile[]=
    {
      "<?xpacket begin=\"%s\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 4.0-c316 44.253921, Sun Oct 01 2006 17:08:23\">\n"
      "   <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      "      <rdf:Description rdf:about=\"\"\n"
      "            xmlns:xap=\"http://ns.adobe.com/xap/1.0/\">\n"
      "         <xap:ModifyDate>%s</xap:ModifyDate>\n"
      "         <xap:CreateDate>%s</xap:CreateDate>\n"
      "         <xap:MetadataDate>%s</xap:MetadataDate>\n"
      "         <xap:CreatorTool>%s</xap:CreatorTool>\n"
      "      </rdf:Description>\n"
      "      <rdf:Description rdf:about=\"\"\n"
      "            xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
      "         <dc:format>application/pdf</dc:format>\n"
      "         <dc:title>\n"
      "           <rdf:Alt>\n"
      "              <rdf:li xml:lang=\"x-default\">%s</rdf:li>\n"
      "           </rdf:Alt>\n"
      "         </dc:title>\n"
      "      </rdf:Description>\n"
      "      <rdf:Description rdf:about=\"\"\n"
      "            xmlns:xapMM=\"http://ns.adobe.com/xap/1.0/mm/\">\n"
      "         <xapMM:DocumentID>uuid:6ec119d7-7982-4f56-808d-dfe64f5b35cf</xapMM:DocumentID>\n"
      "         <xapMM:InstanceID>uuid:a79b99b4-6235-447f-9f6c-ec18ef7555cb</xapMM:InstanceID>\n"
      "      </rdf:Description>\n"
      "      <rdf:Description rdf:about=\"\"\n"
      "            xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n"
      "         <pdf:Producer>%s</pdf:Producer>\n"
      "      </rdf:Description>\n"
      "      <rdf:Description rdf:about=\"\"\n"
      "            xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n"
      "         <pdfaid:part>3</pdfaid:part>\n"
      "         <pdfaid:conformance>B</pdfaid:conformance>\n"
      "      </rdf:Description>\n"
      "   </rdf:RDF>\n"
      "</x:xmpmeta>\n"
      "<?xpacket end=\"w\"?>\n"
    },
    XMPProfileMagick[4]= { (char) 0xef, (char) 0xbb, (char) 0xbf, (char) 0x00 };
RestoreMSCWarning

  char
    basename[MaxTextExtent],
    buffer[MaxTextExtent],
    date[MaxTextExtent],
    *escape,
    **labels,
    page_geometry[MaxTextExtent],
    *url;

  CompressionType
    compression;

  const char
    *device,
    *option,
    *value;

  const StringInfo
    *profile;

  double
    pointsize;

  GeometryInfo
    geometry_info;

  Image
    *next,
    *tile_image;

  MagickBooleanType
    status;

  MagickOffsetType
    offset,
    scene,
    *xref;

  MagickSizeType
    number_pixels;

  MagickStatusType
    flags;

  PointInfo
    delta,
    resolution,
    scale;

  RectangleInfo
    geometry,
    media_info,
    page_info;

  register const IndexPacket
    *indexes;

  register const PixelPacket
    *p;

  register unsigned char
    *q;

  register ssize_t
    i,
    x;

  size_t
    channels,
    imageListLength,
    info_id,
    length,
    object,
    pages_id,
    root_id,
    text_size,
    version;

  ssize_t
    count,
    page_count,
    y;

  struct tm
    utc_time;

  time_t
    seconds;

  unsigned char
    *pixels;

  /*
    Open output image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  status=OpenBlob(image_info,image,WriteBinaryBlobMode,&image->exception);
  if (status == MagickFalse)
    return(status);
  /*
    Allocate X ref memory.
  */
  xref=(MagickOffsetType *) AcquireQuantumMemory(2048UL,sizeof(*xref));
  if (xref == (MagickOffsetType *) NULL)
    ThrowWriterException(ResourceLimitError,"MemoryAllocationFailed");
  (void) memset(xref,0,2048UL*sizeof(*xref));
  /*
    Write Info object.
  */
  object=0;
  version=3;
  if (image_info->compression == JPEG2000Compression)
    version=(size_t) MagickMax(version,5);
  for (next=image; next != (Image *) NULL; next=GetNextImageInList(next))
    if (next->matte != MagickFalse)
      version=(size_t) MagickMax(version,4);
  if (LocaleCompare(image_info->magick,"PDFA") == 0)
    version=(size_t) MagickMax(version,6);
  profile=GetImageProfile(image,"icc");
  if (profile != (StringInfo *) NULL)
    version=(size_t) MagickMax(version,7);
  (void) FormatLocaleString(buffer,MaxTextExtent,"%%PDF-1.%.20g \n",(double)
    version);
  (void) WriteBlobString(image,buffer);
  if (LocaleCompare(image_info->magick,"PDFA") == 0)
    {
      (void) WriteBlobByte(image,'%');
      (void) WriteBlobByte(image,0xe2);
      (void) WriteBlobByte(image,0xe3);
      (void) WriteBlobByte(image,0xcf);
      (void) WriteBlobByte(image,0xd3);
      (void) WriteBlobByte(image,'\n');
    }
  /*
    Write Catalog object.
  */
  xref[object++]=TellBlob(image);
  root_id=object;
  (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
    object);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"<<\n");
  if (LocaleCompare(image_info->magick,"PDFA") != 0)
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Pages %.20g 0 R\n",(double)
      object+1);
  else
    {
      (void) FormatLocaleString(buffer,MaxTextExtent,"/Metadata %.20g 0 R\n",
        (double) object+1);
      (void) WriteBlobString(image,buffer);
      (void) FormatLocaleString(buffer,MaxTextExtent,"/Pages %.20g 0 R\n",
        (double) object+2);
    }
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"/Type /Catalog");
  option=GetImageOption(image_info,"pdf:page-direction");
  if ((option != (const char *) NULL) &&
      (LocaleCompare(option,"right-to-left") == 0))
    (void) WriteBlobString(image,"/ViewerPreferences<</PageDirection/R2L>>\n");
  (void) WriteBlobString(image,"\n");
  (void) WriteBlobString(image,">>\n");
  (void) WriteBlobString(image,"endobj\n");
  GetPathComponent(image->filename,BasePath,basename);
  if (LocaleCompare(image_info->magick,"PDFA") == 0)
    {
      char
        create_date[MaxTextExtent],
        modify_date[MaxTextExtent],
        timestamp[MaxTextExtent],
        xmp_profile[MaxTextExtent];

      /*
        Write XMP object.
      */
      xref[object++]=TellBlob(image);
      (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
        object);
      (void) WriteBlobString(image,buffer);
      (void) WriteBlobString(image,"<<\n");
      (void) WriteBlobString(image,"/Subtype /XML\n");
      *modify_date='\0';
      value=GetImageProperty(image,"date:modify");
      if (value != (const char *) NULL)
        (void) CopyMagickString(modify_date,value,MaxTextExtent);
      *create_date='\0';
      value=GetImageProperty(image,"date:create");
      if (value != (const char *) NULL)
        (void) CopyMagickString(create_date,value,MaxTextExtent);
      (void) FormatMagickTime(GetMagickTime(),MaxTextExtent,timestamp);
      url=(char *) MagickAuthoritativeURL;
      escape=EscapeParenthesis(basename);
      i=FormatLocaleString(xmp_profile,MaxTextExtent,XMPProfile,
        XMPProfileMagick,modify_date,create_date,timestamp,url,escape,url);
      escape=DestroyString(escape);
      (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g\n",(double)
        i);
      (void) WriteBlobString(image,buffer);
      (void) WriteBlobString(image,"/Type /Metadata\n");
      (void) WriteBlobString(image,">>\nstream\n");
      (void) WriteBlobString(image,xmp_profile);
      (void) WriteBlobString(image,"\nendstream\n");
      (void) WriteBlobString(image,"endobj\n");
    }
  /*
    Write Pages object.
  */
  xref[object++]=TellBlob(image);
  pages_id=object;
  (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
    object);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"<<\n");
  (void) WriteBlobString(image,"/Type /Pages\n");
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Kids [ %.20g 0 R ",(double)
    object+1);
  (void) WriteBlobString(image,buffer);
  count=(ssize_t) (pages_id+ObjectsPerImage+1);
  page_count=1;
  if (image_info->adjoin != MagickFalse)
    {
      Image
        *kid_image;

      /*
        Predict page object id's.
      */
      kid_image=image;
      for ( ; GetNextImageInList(kid_image) != (Image *) NULL; count+=ObjectsPerImage)
      {
        page_count++;
        profile=GetImageProfile(kid_image,"icc");
        if (profile != (StringInfo *) NULL)
          count+=2;
        (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 R ",(double)
          count);
        (void) WriteBlobString(image,buffer);
        kid_image=GetNextImageInList(kid_image);
      }
      xref=(MagickOffsetType *) ResizeQuantumMemory(xref,(size_t) count+2048UL,
        sizeof(*xref));
      if (xref == (MagickOffsetType *) NULL)
        ThrowWriterException(ResourceLimitError,"MemoryAllocationFailed");
    }
  (void) WriteBlobString(image,"]\n");
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Count %.20g\n",(double)
    page_count);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,">>\n");
  (void) WriteBlobString(image,"endobj\n");
  scene=0;
  imageListLength=GetImageListLength(image);
  do
  {
    MagickBooleanType
      has_icc_profile;

    profile=GetImageProfile(image,"icc");
    has_icc_profile=(profile != (StringInfo *) NULL) ? MagickTrue : MagickFalse;
    compression=image->compression;
    if (image_info->compression != UndefinedCompression)
      compression=image_info->compression;
    switch (compression)
    {
      case FaxCompression:
      case Group4Compression:
      {
        if ((SetImageMonochrome(image,&image->exception) == MagickFalse) ||
            (image->matte != MagickFalse))
          compression=RLECompression;
        break;
      }
#if !defined(MAGICKCORE_JPEG_DELEGATE)
      case JPEGCompression:
      {
        compression=RLECompression;
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          MissingDelegateError,"DelegateLibrarySupportNotBuiltIn","`%s' (JPEG)",
          image->filename);
        break;
      }
#endif
#if !defined(MAGICKCORE_LIBOPENJP2_DELEGATE)
      case JPEG2000Compression:
      {
        compression=RLECompression;
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          MissingDelegateError,"DelegateLibrarySupportNotBuiltIn","`%s' (JP2)",
          image->filename);
        break;
      }
#endif
#if !defined(MAGICKCORE_ZLIB_DELEGATE)
      case ZipCompression:
      {
        compression=RLECompression;
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          MissingDelegateError,"DelegateLibrarySupportNotBuiltIn","`%s' (ZLIB)",
          image->filename);
        break;
      }
#endif
      case LZWCompression:
      {
        if (LocaleCompare(image_info->magick,"PDFA") == 0)
          compression=RLECompression;  /* LZW compression is forbidden */
        break;
      }
      case NoCompression:
      {
        if (LocaleCompare(image_info->magick,"PDFA") == 0)
          compression=RLECompression; /* ASCII 85 compression is forbidden */
        break;
      }
      default:
        break;
    }
    if (compression == JPEG2000Compression)
      (void) TransformImageColorspace(image,sRGBColorspace);
    /*
      Scale relative to dots-per-inch.
    */
    delta.x=DefaultResolution;
    delta.y=DefaultResolution;
    resolution.x=image->x_resolution;
    resolution.y=image->y_resolution;
    if ((resolution.x == 0.0) || (resolution.y == 0.0))
      {
        flags=ParseGeometry(PSDensityGeometry,&geometry_info);
        resolution.x=geometry_info.rho;
        resolution.y=geometry_info.sigma;
        if ((flags & SigmaValue) == 0)
          resolution.y=resolution.x;
      }
    if (image_info->density != (char *) NULL)
      {
        flags=ParseGeometry(image_info->density,&geometry_info);
        resolution.x=geometry_info.rho;
        resolution.y=geometry_info.sigma;
        if ((flags & SigmaValue) == 0)
          resolution.y=resolution.x;
      }
    if (image->units == PixelsPerCentimeterResolution)
      {
        resolution.x=(double) ((size_t) (100.0*2.54*resolution.x+0.5)/100.0);
        resolution.y=(double) ((size_t) (100.0*2.54*resolution.y+0.5)/100.0);
      }
    SetGeometry(image,&geometry);
    (void) FormatLocaleString(page_geometry,MaxTextExtent,"%.20gx%.20g",(double)
      image->columns,(double) image->rows);
    if (image_info->page != (char *) NULL)
      (void) CopyMagickString(page_geometry,image_info->page,MaxTextExtent);
    else
      if ((image->page.width != 0) && (image->page.height != 0))
        (void) FormatLocaleString(page_geometry,MaxTextExtent,
          "%.20gx%.20g%+.20g%+.20g",(double) image->page.width,(double)
          image->page.height,(double) image->page.x,(double) image->page.y);
      else
        if ((image->gravity != UndefinedGravity) &&
            (LocaleCompare(image_info->magick,"PDF") == 0))
          (void) CopyMagickString(page_geometry,PSPageGeometry,MaxTextExtent);
    (void) ConcatenateMagickString(page_geometry,">",MaxTextExtent);
    (void) ParseMetaGeometry(page_geometry,&geometry.x,&geometry.y,
      &geometry.width,&geometry.height);
    scale.x=(double) (geometry.width*delta.x)/resolution.x;
    geometry.width=(size_t) floor(scale.x+0.5);
    scale.y=(double) (geometry.height*delta.y)/resolution.y;
    geometry.height=(size_t) floor(scale.y+0.5);
    (void) ParseAbsoluteGeometry(page_geometry,&media_info);
    (void) ParseGravityGeometry(image,page_geometry,&page_info,
      &image->exception);
    if (image->gravity != UndefinedGravity)
      {
        geometry.x=(-page_info.x);
        geometry.y=(ssize_t) (media_info.height+page_info.y-image->rows);
      }
    pointsize=12.0;
    if (image_info->pointsize != 0.0)
      pointsize=image_info->pointsize;
    text_size=0;
    value=GetImageProperty(image,"label");
    if (value != (const char *) NULL)
      text_size=(size_t) (MultilineCensus(value)*pointsize+12);
    (void) text_size;
    /*
      Write Page object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    (void) WriteBlobString(image,"/Type /Page\n");
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Parent %.20g 0 R\n",
      (double) pages_id);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"/Resources <<\n");
    labels=(char **) NULL;
    value=GetImageProperty(image,"label");
    if (value != (const char *) NULL)
      labels=StringToList(value);
    if (labels != (char **) NULL)
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,
          "/Font << /F%.20g %.20g 0 R >>\n",(double) image->scene,(double)
          object+4);
        (void) WriteBlobString(image,buffer);
      }
    (void) FormatLocaleString(buffer,MaxTextExtent,
      "/XObject << /Im%.20g %.20g 0 R >>\n",(double) image->scene,(double)
      object+5);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/ProcSet %.20g 0 R >>\n",
      (double) object+3);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,
      "/MediaBox [0 0 %g %g]\n",72.0*media_info.width/resolution.x,
      72.0*media_info.height/resolution.y);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,
      "/CropBox [0 0 %g %g]\n",72.0*media_info.width/resolution.x,
      72.0*media_info.height/resolution.y);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Contents %.20g 0 R\n",
      (double) object+1);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Thumb %.20g 0 R\n",
      (double) object+(has_icc_profile != MagickFalse ? 10 : 8));
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,">>\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Contents object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g 0 R\n",
      (double) object+1);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,">>\n");
    (void) WriteBlobString(image,"stream\n");
    offset=TellBlob(image);
    (void) WriteBlobString(image,"q\n");
    if (labels != (char **) NULL)
      for (i=0; labels[i] != (char *) NULL; i++)
      {
        (void) WriteBlobString(image,"BT\n");
        (void) FormatLocaleString(buffer,MaxTextExtent,"/F%.20g %g Tf\n",
          (double) image->scene,pointsize);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g %.20g Td\n",
          (double) geometry.x,(double) (geometry.y+geometry.height+i*pointsize+
          12));
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"(%s) Tj\n",labels[i]);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,"ET\n");
        labels[i]=DestroyString(labels[i]);
      }
    (void) FormatLocaleString(buffer,MaxTextExtent,"%g 0 0 %g %.20g %.20g cm\n",
      scale.x,scale.y,(double) geometry.x,(double) geometry.y);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Im%.20g Do\n",(double)
      image->scene);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"Q\n");
    offset=TellBlob(image)-offset;
    (void) WriteBlobString(image,"\nendstream\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Length object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double) offset);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Procset object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    if ((image->storage_class == DirectClass) || (image->colors > 256))
      (void) CopyMagickString(buffer,"[ /PDF /Text /ImageC",MaxTextExtent);
    else
      if ((compression == FaxCompression) || (compression == Group4Compression))
        (void) CopyMagickString(buffer,"[ /PDF /Text /ImageB",MaxTextExtent);
      else
        (void) CopyMagickString(buffer,"[ /PDF /Text /ImageI",MaxTextExtent);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image," ]\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Font object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    if (labels != (char **) NULL)
      {
        (void) WriteBlobString(image,"/Type /Font\n");
        (void) WriteBlobString(image,"/Subtype /Type1\n");
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Name /F%.20g\n",
          (double) image->scene);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,"/BaseFont /Helvetica\n");
        (void) WriteBlobString(image,"/Encoding /MacRomanEncoding\n");
        labels=(char **) RelinquishMagickMemory(labels);
      }
    (void) WriteBlobString(image,">>\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write XObject object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    (void) WriteBlobString(image,"/Type /XObject\n");
    (void) WriteBlobString(image,"/Subtype /Image\n");
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Name /Im%.20g\n",(double)
      image->scene);
    (void) WriteBlobString(image,buffer);
    switch (compression)
    {
      case NoCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"ASCII85Decode");
        break;
      }
      case JPEGCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"DCTDecode");
        if (image->colorspace != CMYKColorspace)
          break;
        (void) WriteBlobString(image,buffer);
        (void) CopyMagickString(buffer,"/Decode [1 0 1 0 1 0 1 0]\n",
          MaxTextExtent);
        break;
      }
      case JPEG2000Compression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"JPXDecode");
        if (image->colorspace != CMYKColorspace)
          break;
        (void) WriteBlobString(image,buffer);
        (void) CopyMagickString(buffer,"/Decode [1 0 1 0 1 0 1 0]\n",
          MaxTextExtent);
        break;
      }
      case LZWCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"LZWDecode");
        break;
      }
      case ZipCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"FlateDecode");
        break;
      }
      case FaxCompression:
      case Group4Compression:
      {
        (void) CopyMagickString(buffer,"/Filter [ /CCITTFaxDecode ]\n",
          MaxTextExtent);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"/DecodeParms [ << "
          "/K %s /BlackIs1 false /Columns %.20g /Rows %.20g >> ]\n",CCITTParam,
          (double) image->columns,(double) image->rows);
        break;
      }
      default:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,
          "RunLengthDecode");
        break;
      }
    }
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Width %.20g\n",(double)
      image->columns);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Height %.20g\n",(double)
      image->rows);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/ColorSpace %.20g 0 R\n",
      (double) object+2);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/BitsPerComponent %d\n",
      (compression == FaxCompression) || (compression == Group4Compression) ?
      1 : 8);
    (void) WriteBlobString(image,buffer);
    if (image->matte != MagickFalse)
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,"/SMask %.20g 0 R\n",
          (double) object+(has_icc_profile != MagickFalse ? 9 : 7));
        (void) WriteBlobString(image,buffer);
      }
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g 0 R\n",
      (double) object+1);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,">>\n");
    (void) WriteBlobString(image,"stream\n");
    offset=TellBlob(image);
    number_pixels=(MagickSizeType) image->columns*image->rows;
    if ((4*number_pixels) != (MagickSizeType) ((size_t) (4*number_pixels)))
      ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
    if ((compression == FaxCompression) || (compression == Group4Compression) ||
        ((image_info->type != TrueColorType) &&
         (SetImageGray(image,&image->exception) != MagickFalse)))
      {
        switch (compression)
        {
          case FaxCompression:
          case Group4Compression:
          {
            if (LocaleCompare(CCITTParam,"0") == 0)
              {
                (void) HuffmanEncodeImage(image_info,image,image);
                break;
              }
            (void) Huffman2DEncodeImage(image_info,image,image);
            break;
          }
          case JPEGCompression:
          {
            status=InjectImageBlob(image_info,image,image,"jpeg",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,image->exception.reason);
            break;
          }
          case JPEG2000Compression:
          {
            status=InjectImageBlob(image_info,image,image,"jp2",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,image->exception.reason);
            break;
          }
          case RLECompression:
          default:
          {
            MemoryInfo
              *pixel_info;

            /*
              Allocate pixel array.
            */
            length=(size_t) number_pixels;
            pixel_info=AcquireVirtualMemory(length,sizeof(*pixels));
            if (pixel_info == (MemoryInfo *) NULL)
              ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
            pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
            /*
              Dump Runlength encoded pixels.
            */
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                *q++=ScaleQuantumToChar(ClampToQuantum(
                  GetPixelLuma(image,p)));
                p++;
              }
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
            if (compression == ZipCompression)
              status=ZLIBEncodeImage(image,length,pixels);
            else
#endif
              if (compression == LZWCompression)
                status=LZWEncodeImage(image,length,pixels);
              else
                status=PackbitsEncodeImage(image,length,pixels);
            pixel_info=RelinquishVirtualMemory(pixel_info);
            if (status == MagickFalse)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                (void) CloseBlob(image);
                return(MagickFalse);
              }
            break;
          }
          case NoCompression:
          {
            /*
              Dump uncompressed PseudoColor packets.
            */
            Ascii85Initialize(image);
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                Ascii85Encode(image,ScaleQuantumToChar(ClampToQuantum(
                  GetPixelLuma(image,p))));
                p++;
              }
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
            Ascii85Flush(image);
            break;
          }
        }
      }
    else
      if ((image->storage_class == DirectClass) || (image->colors > 256) ||
          (compression == JPEGCompression) ||
          (compression == JPEG2000Compression))
        switch (compression)
        {
          case JPEGCompression:
          {
            status=InjectImageBlob(image_info,image,image,"jpeg",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,image->exception.reason);
            break;
          }
          case JPEG2000Compression:
          {
            status=InjectImageBlob(image_info,image,image,"jp2",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,image->exception.reason);
            break;
          }
          case RLECompression:
          default:
          {
            MemoryInfo
              *pixel_info;

            /*
              Allocate pixel array.
            */
            length=(size_t) number_pixels;
            length*=image->colorspace == CMYKColorspace ? 4UL : 3UL;
            pixel_info=AcquireVirtualMemory(length,sizeof(*pixels));
            if (pixel_info == (MemoryInfo *) NULL)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
              }
            pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
            /*
              Dump runlength encoded pixels.
            */
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              indexes=GetVirtualIndexQueue(image);
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                *q++=ScaleQuantumToChar(GetPixelRed(p));
                *q++=ScaleQuantumToChar(GetPixelGreen(p));
                *q++=ScaleQuantumToChar(GetPixelBlue(p));
                if (image->colorspace == CMYKColorspace)
                  *q++=ScaleQuantumToChar(GetPixelIndex(indexes+x));
                p++;
              }
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
            if (compression == ZipCompression)
              status=ZLIBEncodeImage(image,length,pixels);
            else
#endif
              if (compression == LZWCompression)
                status=LZWEncodeImage(image,length,pixels);
              else
                status=PackbitsEncodeImage(image,length,pixels);
            pixel_info=RelinquishVirtualMemory(pixel_info);
            if (status == MagickFalse)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                (void) CloseBlob(image);
                return(MagickFalse);
              }
            break;
          }
          case NoCompression:
          {
            /*
              Dump uncompressed DirectColor packets.
            */
            Ascii85Initialize(image);
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              indexes=GetVirtualIndexQueue(image);
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelRed(p)));
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelGreen(p)));
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelBlue(p)));
                if (image->colorspace == CMYKColorspace)
                  Ascii85Encode(image,ScaleQuantumToChar(
                    GetPixelIndex(indexes+x)));
                p++;
              }
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
            Ascii85Flush(image);
            break;
          }
        }
      else
        {
          /*
            Dump number of colors and colormap.
          */
          switch (compression)
          {
            case RLECompression:
            default:
            {
              MemoryInfo
                *pixel_info;

              /*
                Allocate pixel array.
              */
              length=(size_t) number_pixels;
              pixel_info=AcquireVirtualMemory(length,sizeof(*pixels));
              if (pixel_info == (MemoryInfo *) NULL)
                {
                  xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                  ThrowPDFException(ResourceLimitError,
                    "MemoryAllocationFailed");
                }
              pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
              /*
                Dump runlength encoded pixels.
              */
              q=pixels;
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,
                  &image->exception);
                if (p == (const PixelPacket *) NULL)
                  break;
                indexes=GetVirtualIndexQueue(image);
                for (x=0; x < (ssize_t) image->columns; x++)
                  *q++=(unsigned char) GetPixelIndex(indexes+x);
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
              if (compression == ZipCompression)
                status=ZLIBEncodeImage(image,length,pixels);
              else
#endif
                if (compression == LZWCompression)
                  status=LZWEncodeImage(image,length,pixels);
                else
                  status=PackbitsEncodeImage(image,length,pixels);
              pixel_info=RelinquishVirtualMemory(pixel_info);
              if (status == MagickFalse)
                {
                  xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                  (void) CloseBlob(image);
                  return(MagickFalse);
                }
              break;
            }
            case NoCompression:
            {
              /*
                Dump uncompressed PseudoColor packets.
              */
              Ascii85Initialize(image);
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,
                  &image->exception);
                if (p == (const PixelPacket *) NULL)
                  break;
                indexes=GetVirtualIndexQueue(image);
                for (x=0; x < (ssize_t) image->columns; x++)
                  Ascii85Encode(image,(unsigned char)
                    GetPixelIndex(indexes+x));
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
              Ascii85Flush(image);
              break;
            }
          }
        }
    offset=TellBlob(image)-offset;
    (void) WriteBlobString(image,"\nendstream\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Length object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double) offset);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Colorspace object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    device="DeviceRGB";
    channels=0;
    if (image->colorspace == CMYKColorspace)
      {
        device="DeviceCMYK";
        channels=4;
      }
    else
      if ((compression == FaxCompression) ||
          (compression == Group4Compression) ||
          ((image_info->type != TrueColorType) &&
           (SetImageGray(image,&image->exception) != MagickFalse)))
        {
          device="DeviceGray";
          channels=1;
        }
      else
        if ((image->storage_class == DirectClass) ||
            (image->colors > 256) || (compression == JPEGCompression) ||
            (compression == JPEG2000Compression))
          {
            device="DeviceRGB";
            channels=3;
          }
    profile=GetImageProfile(image,"icc");
    if ((profile == (StringInfo *) NULL) || (channels == 0))
      {
        if (channels != 0)
          (void) FormatLocaleString(buffer,MaxTextExtent,"/%s\n",device);
        else
          (void) FormatLocaleString(buffer,MaxTextExtent,
            "[ /Indexed /%s %.20g %.20g 0 R ]\n",device,(double) image->colors-
            1,(double) object+3);
        (void) WriteBlobString(image,buffer);
      }
    else
      {
        const unsigned char
          *p;

        /*
          Write ICC profile.
        */
        (void) FormatLocaleString(buffer,MaxTextExtent,
          "[/ICCBased %.20g 0 R]\n",(double) object+1);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,"endobj\n");
        xref[object++]=TellBlob(image);
        (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",
          (double) object);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"<<\n/N %.20g\n"
          "/Filter /ASCII85Decode\n/Length %.20g 0 R\n/Alternate /%s\n>>\n"
          "stream\n",(double) channels,(double) object+1,device);
        (void) WriteBlobString(image,buffer);
        offset=TellBlob(image);
        Ascii85Initialize(image);
        p=GetStringInfoDatum(profile);
        for (i=0; i < (ssize_t) GetStringInfoLength(profile); i++)
          Ascii85Encode(image,(unsigned char) *p++);
        Ascii85Flush(image);
        offset=TellBlob(image)-offset;
        (void) WriteBlobString(image,"endstream\n");
        (void) WriteBlobString(image,"endobj\n");
        /*
          Write Length object.
        */
        xref[object++]=TellBlob(image);
        (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",
          (double) object);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double)
          offset);
        (void) WriteBlobString(image,buffer);
      }
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Thumb object.
    */
    SetGeometry(image,&geometry);
    (void) ParseMetaGeometry("106x106+0+0>",&geometry.x,&geometry.y,
      &geometry.width,&geometry.height);
    tile_image=ThumbnailImage(image,geometry.width,geometry.height,
      &image->exception);
    if (tile_image == (Image *) NULL)
      ThrowPDFException(ResourceLimitError,image->exception.reason);
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    switch (compression)
    {
      case NoCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"ASCII85Decode");
        break;
      }
      case JPEGCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"DCTDecode");
        if (image->colorspace != CMYKColorspace)
          break;
        (void) WriteBlobString(image,buffer);
        (void) CopyMagickString(buffer,"/Decode [1 0 1 0 1 0 1 0]\n",
          MaxTextExtent);
        break;
      }
      case JPEG2000Compression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"JPXDecode");
        if (image->colorspace != CMYKColorspace)
          break;
        (void) WriteBlobString(image,buffer);
        (void) CopyMagickString(buffer,"/Decode [1 0 1 0 1 0 1 0]\n",
          MaxTextExtent);
        break;
      }
      case LZWCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"LZWDecode");
        break;
      }
      case ZipCompression:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"FlateDecode");
        break;
      }
      case FaxCompression:
      case Group4Compression:
      {
        (void) CopyMagickString(buffer,"/Filter [ /CCITTFaxDecode ]\n",
          MaxTextExtent);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"/DecodeParms [ << "
          "/K %s /BlackIs1 false /Columns %.20g /Rows %.20g >> ]\n",CCITTParam,
          (double) tile_image->columns,(double) tile_image->rows);
        break;
      }
      default:
      {
        (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,
          "RunLengthDecode");
        break;
      }
    }
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Width %.20g\n",(double)
      tile_image->columns);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Height %.20g\n",(double)
      tile_image->rows);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/ColorSpace %.20g 0 R\n",
      (double) object-(has_icc_profile != MagickFalse ? 3 : 1));
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/BitsPerComponent %d\n",
      (compression == FaxCompression) || (compression == Group4Compression) ?
      1 : 8);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g 0 R\n",
      (double) object+1);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,">>\n");
    (void) WriteBlobString(image,"stream\n");
    offset=TellBlob(image);
    number_pixels=(MagickSizeType) tile_image->columns*tile_image->rows;
    if ((compression == FaxCompression) ||
        (compression == Group4Compression) ||
        ((image_info->type != TrueColorType) &&
         (SetImageGray(tile_image,&image->exception) != MagickFalse)))
      {
        switch (compression)
        {
          case FaxCompression:
          case Group4Compression:
          {
            if (LocaleCompare(CCITTParam,"0") == 0)
              {
                (void) HuffmanEncodeImage(image_info,image,tile_image);
                break;
              }
            (void) Huffman2DEncodeImage(image_info,image,tile_image);
            break;
          }
          case JPEGCompression:
          {
            status=InjectImageBlob(image_info,image,tile_image,"jpeg",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,tile_image->exception.reason);
            break;
          }
          case JPEG2000Compression:
          {
            status=InjectImageBlob(image_info,image,tile_image,"jp2",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,tile_image->exception.reason);
            break;
          }
          case RLECompression:
          default:
          {
            MemoryInfo
              *pixel_info;

            /*
              Allocate pixel array.
            */
            length=(size_t) number_pixels;
            pixel_info=AcquireVirtualMemory(length,sizeof(*pixels));
            if (pixel_info == (MemoryInfo *) NULL)
              {
                tile_image=DestroyImage(tile_image);
                ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
              }
            pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
            /*
              Dump Runlength encoded pixels.
            */
            q=pixels;
            for (y=0; y < (ssize_t) tile_image->rows; y++)
            {
              p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                &tile_image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) tile_image->columns; x++)
              {
                *q++=ScaleQuantumToChar(ClampToQuantum(
                   GetPixelLuma(tile_image,p)));
                p++;
              }
            }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
            if (compression == ZipCompression)
              status=ZLIBEncodeImage(image,length,pixels);
            else
#endif
              if (compression == LZWCompression)
                status=LZWEncodeImage(image,length,pixels);
              else
                status=PackbitsEncodeImage(image,length,pixels);
            pixel_info=RelinquishVirtualMemory(pixel_info);
            if (status == MagickFalse)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                (void) CloseBlob(image);
                return(MagickFalse);
              }
            break;
          }
          case NoCompression:
          {
            /*
              Dump uncompressed PseudoColor packets.
            */
            Ascii85Initialize(image);
            for (y=0; y < (ssize_t) tile_image->rows; y++)
            {
              p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                &tile_image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) tile_image->columns; x++)
              {
                Ascii85Encode(image,ScaleQuantumToChar(ClampToQuantum(
                  GetPixelLuma(tile_image,p))));
                p++;
              }
            }
            Ascii85Flush(image);
            break;
          }
        }
      }
    else
      if ((tile_image->storage_class == DirectClass) ||
          (tile_image->colors > 256) || (compression == JPEGCompression) ||
          (compression == JPEG2000Compression))
        switch (compression)
        {
          case JPEGCompression:
          {
            status=InjectImageBlob(image_info,image,tile_image,"jpeg",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,tile_image->exception.reason);
            break;
          }
          case JPEG2000Compression:
          {
            status=InjectImageBlob(image_info,image,tile_image,"jp2",
              &image->exception);
            if (status == MagickFalse)
              ThrowPDFException(CoderError,tile_image->exception.reason);
            break;
          }
          case RLECompression:
          default:
          {
            MemoryInfo
              *pixel_info;

            /*
              Allocate pixel array.
            */
            length=(size_t) number_pixels;
            length*=tile_image->colorspace == CMYKColorspace ? 4UL : 3UL;
            pixel_info=AcquireVirtualMemory(length,4*sizeof(*pixels));
            if (pixel_info == (MemoryInfo *) NULL)
              {
                tile_image=DestroyImage(tile_image);
                ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
              }
            pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
            /*
              Dump runoffset encoded pixels.
            */
            q=pixels;
            for (y=0; y < (ssize_t) tile_image->rows; y++)
            {
              p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                &tile_image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              indexes=GetVirtualIndexQueue(tile_image);
              for (x=0; x < (ssize_t) tile_image->columns; x++)
              {
                *q++=ScaleQuantumToChar(GetPixelRed(p));
                *q++=ScaleQuantumToChar(GetPixelGreen(p));
                *q++=ScaleQuantumToChar(GetPixelBlue(p));
                if (tile_image->colorspace == CMYKColorspace)
                  *q++=ScaleQuantumToChar(GetPixelIndex(indexes+x));
                p++;
              }
            }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
            if (compression == ZipCompression)
              status=ZLIBEncodeImage(image,length,pixels);
            else
#endif
              if (compression == LZWCompression)
                status=LZWEncodeImage(image,length,pixels);
              else
                status=PackbitsEncodeImage(image,length,pixels);
            pixel_info=RelinquishVirtualMemory(pixel_info);
            if (status == MagickFalse)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                (void) CloseBlob(image);
                return(MagickFalse);
              }
            break;
          }
          case NoCompression:
          {
            /*
              Dump uncompressed DirectColor packets.
            */
            Ascii85Initialize(image);
            for (y=0; y < (ssize_t) tile_image->rows; y++)
            {
              p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                &tile_image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              indexes=GetVirtualIndexQueue(tile_image);
              for (x=0; x < (ssize_t) tile_image->columns; x++)
              {
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelRed(p)));
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelGreen(p)));
                Ascii85Encode(image,ScaleQuantumToChar(
                  GetPixelBlue(p)));
                if (image->colorspace == CMYKColorspace)
                  Ascii85Encode(image,ScaleQuantumToChar(
                    GetPixelIndex(indexes+x)));
                p++;
              }
            }
            Ascii85Flush(image);
            break;
          }
        }
      else
        {
          /*
            Dump number of colors and colormap.
          */
          switch (compression)
          {
            case RLECompression:
            default:
            {
              MemoryInfo
                *pixel_info;

              /*
                Allocate pixel array.
              */
              length=(size_t) number_pixels;
              pixel_info=AcquireVirtualMemory(length,sizeof(*pixels));
              if (pixel_info == (MemoryInfo *) NULL)
                {
                  tile_image=DestroyImage(tile_image);
                  ThrowPDFException(ResourceLimitError,
                    "MemoryAllocationFailed");
                }
              pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
              /*
                Dump Runlength encoded pixels.
              */
              q=pixels;
              for (y=0; y < (ssize_t) tile_image->rows; y++)
              {
                p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                  &tile_image->exception);
                if (p == (const PixelPacket *) NULL)
                  break;
                indexes=GetVirtualIndexQueue(tile_image);
                for (x=0; x < (ssize_t) tile_image->columns; x++)
                  *q++=(unsigned char) GetPixelIndex(indexes+x);
              }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
              if (compression == ZipCompression)
                status=ZLIBEncodeImage(image,length,pixels);
              else
#endif
                if (compression == LZWCompression)
                  status=LZWEncodeImage(image,length,pixels);
                else
                  status=PackbitsEncodeImage(image,length,pixels);
              pixel_info=RelinquishVirtualMemory(pixel_info);
              if (status == MagickFalse)
                {
                  xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                  (void) CloseBlob(image);
                  return(MagickFalse);
                }
              break;
            }
            case NoCompression:
            {
              /*
                Dump uncompressed PseudoColor packets.
              */
              Ascii85Initialize(image);
              for (y=0; y < (ssize_t) tile_image->rows; y++)
              {
                p=GetVirtualPixels(tile_image,0,y,tile_image->columns,1,
                  &tile_image->exception);
                if (p == (const PixelPacket *) NULL)
                  break;
                indexes=GetVirtualIndexQueue(tile_image);
                for (x=0; x < (ssize_t) tile_image->columns; x++)
                  Ascii85Encode(image,(unsigned char) GetPixelIndex(indexes+x));
              }
              Ascii85Flush(image);
              break;
            }
          }
        }
    tile_image=DestroyImage(tile_image);
    offset=TellBlob(image)-offset;
    (void) WriteBlobString(image,"\nendstream\n");
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Length object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double) offset);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"endobj\n");
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    if ((image->storage_class == DirectClass) || (image->colors > 256) ||
        (compression == FaxCompression) || (compression == Group4Compression))
      (void) WriteBlobString(image,">>\n");
    else
      {
        /*
          Write Colormap object.
        */
        if (compression == NoCompression)
          (void) WriteBlobString(image,"/Filter [ /ASCII85Decode ]\n");
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g 0 R\n",
          (double) object+1);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,">>\n");
        (void) WriteBlobString(image,"stream\n");
        offset=TellBlob(image);
        if (compression == NoCompression)
          Ascii85Initialize(image);
        for (i=0; i < (ssize_t) image->colors; i++)
        {
          if (compression == NoCompression)
            {
              Ascii85Encode(image,ScaleQuantumToChar(image->colormap[i].red));
              Ascii85Encode(image,ScaleQuantumToChar(image->colormap[i].green));
              Ascii85Encode(image,ScaleQuantumToChar(image->colormap[i].blue));
              continue;
            }
          (void) WriteBlobByte(image,
            ScaleQuantumToChar(image->colormap[i].red));
          (void) WriteBlobByte(image,
            ScaleQuantumToChar(image->colormap[i].green));
          (void) WriteBlobByte(image,
            ScaleQuantumToChar(image->colormap[i].blue));
        }
        if (compression == NoCompression)
          Ascii85Flush(image);
       offset=TellBlob(image)-offset;
       (void) WriteBlobString(image,"\nendstream\n");
      }
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Length object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double)
      offset);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write softmask object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"<<\n");
    if (image->matte == MagickFalse)
      (void) WriteBlobString(image,">>\n");
    else
      {
        (void) WriteBlobString(image,"/Type /XObject\n");
        (void) WriteBlobString(image,"/Subtype /Image\n");
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Name /Ma%.20g\n",
          (double) image->scene);
        (void) WriteBlobString(image,buffer);
        switch (compression)
        {
          case NoCompression:
          {
            (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,
              "ASCII85Decode");
            break;
          }
          case LZWCompression:
          {
            (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,"LZWDecode");
            break;
          }
          case ZipCompression:
          {
            (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,
              "FlateDecode");
            break;
          }
          default:
          {
            (void) FormatLocaleString(buffer,MaxTextExtent,CFormat,
              "RunLengthDecode");
            break;
          }
        }
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Width %.20g\n",(double)
          image->columns);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Height %.20g\n",
          (double) image->rows);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,"/ColorSpace /DeviceGray\n");
        (void) FormatLocaleString(buffer,MaxTextExtent,"/BitsPerComponent %d\n",
          (compression == FaxCompression) || (compression == Group4Compression)
          ? 1 : 8);
        (void) WriteBlobString(image,buffer);
        (void) FormatLocaleString(buffer,MaxTextExtent,"/Length %.20g 0 R\n",
          (double) object+1);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,">>\n");
        (void) WriteBlobString(image,"stream\n");
        offset=TellBlob(image);
        number_pixels=(MagickSizeType) image->columns*image->rows;
        switch (compression)
        {
          case RLECompression:
          default:
          {
            MemoryInfo
              *pixel_info;

            /*
              Allocate pixel array.
            */
            length=(size_t) number_pixels;
            pixel_info=AcquireVirtualMemory(length,4*sizeof(*pixels));
            if (pixel_info == (MemoryInfo *) NULL)
              {
                image=DestroyImage(image);
                ThrowPDFException(ResourceLimitError,"MemoryAllocationFailed");
              }
           pixels=(unsigned char *) GetVirtualMemoryBlob(pixel_info);
            /*
              Dump Runlength encoded pixels.
            */
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                *q++=ScaleQuantumToChar((Quantum) (GetPixelAlpha(p)));
                p++;
              }
            }
#if defined(MAGICKCORE_ZLIB_DELEGATE)
            if (compression == ZipCompression)
              status=ZLIBEncodeImage(image,length,pixels);
            else
#endif
              if (compression == LZWCompression)
                status=LZWEncodeImage(image,length,pixels);
              else
                status=PackbitsEncodeImage(image,length,pixels);
            pixel_info=RelinquishVirtualMemory(pixel_info);
            if (status == MagickFalse)
              {
                xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
                (void) CloseBlob(image);
                return(MagickFalse);
              }
            break;
          }
          case NoCompression:
          {
            /*
              Dump uncompressed PseudoColor packets.
            */
            Ascii85Initialize(image);
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,&image->exception);
              if (p == (const PixelPacket *) NULL)
                break;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                Ascii85Encode(image,ScaleQuantumToChar((Quantum) (QuantumRange-
                  GetPixelOpacity(p))));
                p++;
              }
            }
            Ascii85Flush(image);
            break;
          }
        }
        offset=TellBlob(image)-offset;
        (void) WriteBlobString(image,"\nendstream\n");
      }
    (void) WriteBlobString(image,"endobj\n");
    /*
      Write Length object.
    */
    xref[object++]=TellBlob(image);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
      object);
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double) offset);
    (void) WriteBlobString(image,buffer);
    (void) WriteBlobString(image,"endobj\n");
    if (GetNextImageInList(image) == (Image *) NULL)
      break;
    image=SyncNextImageInList(image);
    status=SetImageProgress(image,SaveImagesTag,scene++,imageListLength);
    if (status == MagickFalse)
      break;
  } while (image_info->adjoin != MagickFalse);
  /*
    Write Metadata object.
  */
  xref[object++]=TellBlob(image);
  info_id=object;
  (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g 0 obj\n",(double)
    object);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"<<\n");
  if (LocaleCompare(image_info->magick,"PDFA") == 0)
    {
      escape=EscapeParenthesis(basename);
      (void) FormatLocaleString(buffer,MagickPathExtent,"/Title (%s)\n",
        escape);
      escape=DestroyString(escape);
    }
  else
    {
      wchar_t
        *utf16;

      utf16=ConvertUTF8ToUTF16((unsigned char *) basename,&length);
      if (utf16 != (wchar_t *) NULL)
        {
          unsigned char
            hex_digits[16];

          hex_digits[0]='0';
          hex_digits[1]='1';
          hex_digits[2]='2';
          hex_digits[3]='3';
          hex_digits[4]='4';
          hex_digits[5]='5';
          hex_digits[6]='6';
          hex_digits[7]='7';
          hex_digits[8]='8';
          hex_digits[9]='9';
          hex_digits[10]='A';
          hex_digits[11]='B';
          hex_digits[12]='C';
          hex_digits[13]='D';
          hex_digits[14]='E';
          hex_digits[15]='F';
          (void) FormatLocaleString(buffer,MagickPathExtent,"/Title <FEFF");
          (void) WriteBlobString(image,buffer);
          for (i=0; i < (ssize_t) length; i++)
          {
            (void) WriteBlobByte(image,'0');
            (void) WriteBlobByte(image,'0');
            (void) WriteBlobByte(image,hex_digits[(utf16[i] >> 4) & 0x0f]);
            (void) WriteBlobByte(image,hex_digits[utf16[i] & 0x0f]);
          }
          (void) FormatLocaleString(buffer,MagickPathExtent,">\n");
          utf16=(wchar_t *) RelinquishMagickMemory(utf16);
        }
    }
  (void) WriteBlobString(image,buffer);
  seconds=GetMagickTime();
  GetMagickUTCtime(&seconds,&utc_time);
  (void) FormatLocaleString(date,MaxTextExtent,"D:%04d%02d%02d%02d%02d%02d",
    utc_time.tm_year+1900,utc_time.tm_mon+1,utc_time.tm_mday,
    utc_time.tm_hour,utc_time.tm_min,utc_time.tm_sec);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/CreationDate (%s)\n",date);
  (void) WriteBlobString(image,buffer);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/ModDate (%s)\n",date);
  (void) WriteBlobString(image,buffer);
  url=(char *) MagickAuthoritativeURL;
  escape=EscapeParenthesis(url);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Producer (%s)\n",escape);
  escape=DestroyString(escape);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,">>\n");
  (void) WriteBlobString(image,"endobj\n");
  /*
    Write Xref object.
  */
  offset=TellBlob(image)-xref[0]+
   (LocaleCompare(image_info->magick,"PDFA") == 0 ? 6 : 0)+10;
  (void) WriteBlobString(image,"xref\n");
  (void) FormatLocaleString(buffer,MaxTextExtent,"0 %.20g\n",(double)
    object+1);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"0000000000 65535 f \n");
  for (i=0; i < (ssize_t) object; i++)
  {
    (void) FormatLocaleString(buffer,MaxTextExtent,"%010lu 00000 n \n",
      (unsigned long) xref[i]);
    (void) WriteBlobString(image,buffer);
  }
  (void) WriteBlobString(image,"trailer\n");
  (void) WriteBlobString(image,"<<\n");
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Size %.20g\n",(double)
    object+1);
  (void) WriteBlobString(image,buffer);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Info %.20g 0 R\n",(double)
    info_id);
  (void) WriteBlobString(image,buffer);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/Root %.20g 0 R\n",(double)
    root_id);
  (void) WriteBlobString(image,buffer);
  (void) SignatureImage(image);
  (void) FormatLocaleString(buffer,MaxTextExtent,"/ID [<%s> <%s>]\n",
    GetImageProperty(image,"signature"),GetImageProperty(image,"signature"));
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,">>\n");
  (void) WriteBlobString(image,"startxref\n");
  (void) FormatLocaleString(buffer,MaxTextExtent,"%.20g\n",(double) offset);
  (void) WriteBlobString(image,buffer);
  (void) WriteBlobString(image,"%%EOF\n");
  xref=(MagickOffsetType *) RelinquishMagickMemory(xref);
  (void) CloseBlob(image);
  return(MagickTrue);
}
