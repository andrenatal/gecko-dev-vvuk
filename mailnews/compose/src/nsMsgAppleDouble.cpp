/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

/*
*
*   apple-double.c
*	--------------
*
*  	  The codes to do apple double encoding/decoding.
*		
*		02aug95		mym		created.
*		27sep95		mym		Add the XP_Mac to ensure the cross-platform.
*		
*/
#include "nsID.h"
#include "nsCRT.h"
#include "nscore.h"
#include "msgCore.h"
#include "nsMsgAppleDouble.h"
#include "nsMsgAppleCodes.h"
#include "nsFileSpec.h"
#include "nsMsgCompUtils.h"

#ifdef XP_MAC

#pragma warn_unusedarg off
#include "m_cvstrm.h"

#pragma cplusplus on
//RICHIEDELETE #include "InternetConfig.h"
//RICHIEDELETE #include "ufilemgr.h"
//RICHIEDELETE #include "BufferStream.h"
//RICHIEDELETE #include "Umimemap.h"
//RICHIEDELETE #include "uprefd.h"
//RICHIEDELETE #include "ulaunch.h"
void DecodingDone( appledouble_decode_object* p_ap_decode_obj );

OSErr my_FSSpecFromPathname(char* src_filename, FSSpec* fspec)
{
	/* don't resolve aliases... */
	return CFileMgr::FSSpecFromLocalUnixPath(src_filename, fspec, false);
}

char* my_PathnameFromFSSpec(FSSpec* fspec)
{
	return CFileMgr::GetURLFromFileSpec(*fspec);
}

//
// Returns true if the resource fork should be sent!
//
// RICHIEHACK: for now, this is a temporary solution that should be
// replaced by calls to the MIME service! We look for files that typically
// don't have resource forks and if it is one of these types, we go with
// it, but otherwise, we are going to do the fancy encoding!
//
PRBool	
nsMsgIsMacFile(char *aUrlString)
{
	Boolean returnValue = PR_FALSE;

  char  *ext = nsMsgGetExtensionFromFileURL(nsString(aUrlString));
  if ( (!ext) || (!*ext) )
    return PR_TRUE;

  if (
       (!PL_strcasecmp(ext, "JPG")) ||
       (!PL_strcasecmp(ext, "GIF")) ||
       (!PL_strcasecmp(ext, "TIF")) ||
       (!PL_strcasecmp(ext, "HTM")) ||
       (!PL_strcasecmp(ext, "HTML")) ||
       (!PL_strcasecmp(ext, "ART")) ||
       (!PL_strcasecmp(ext, "XUL")) ||
       (!PL_strcasecmp(ext, "XML")) ||
       (!PL_strcasecmp(ext, "XUL"))
     )
     return PR_FALSE;
  else
    return PR_TRUE;
}

/* Netlib utility routine, should be ripped out */
void	MacGetFileType(nsFileSpec   *path, 
                     PRBool       *useDefault, 
                     char         **fileType, 
                     char         **encoding)
{
	if ((path == NULL) || (fileType == NULL) || (encoding == NULL))
		return;

	*useDefault = TRUE;
	*fileType = NULL;
	*encoding = NULL;

	char *pathPart = NET_ParseURL( path, GET_PATH_PART);
	if (pathPart == NULL)
		return;

	nsFilePath thePath(pathPart);
	nsNativeFileSpec spec(thePath);
	XP_FREE(pathPart);

	CMimeMapper * mapper = CPrefs::sMimeTypes.FindMimeType(spec);
	if (mapper != NULL)
	{
		*useDefault = FALSE;
		*fileType = nsCRT::strdup(mapper->GetMimeName());
	}
	else
	{
		FInfo		fndrInfo;
		OSErr err = FSpGetFInfo( &spec, &fndrInfo );
		if ( (err != noErr) || (fndrInfo.fdType == 'TEXT') )
      *fileType = nsCRT::strdup(APPLICATION_OCTET_STREAM);
		else
		{
			// Time to call IC to see if it knows anything
			ICMapEntry ICMapper;
			
			ICError  error = 0;
			CStr255 fileName( spec.name );
			error = CInternetConfigInterface::GetInternetConfigFileMapping(
					fndrInfo.fdType, fndrInfo.fdCreator,  fileName ,  &ICMapper );	
			if( error != icPrefNotFoundErr && StrLength(ICMapper.MIME_type) )
			{
				*useDefault = FALSE;
				CStr255 mimeName( ICMapper.MIME_type );
				*fileType = nsCRT::strdup( mimeName );
			}
			else
			{
				// That failed try using the creator type		
				mapper = CPrefs::sMimeTypes.FindCreator(fndrInfo.fdCreator);
				if( mapper)
				{
					*useDefault = FALSE;
					*fileType = nsCRT::strdup(mapper->GetMimeName());
				}
				else
				{
					// don't have a mime mapper
					*fileType = nsCRT::strdup(APPLICATION_OCTET_STREAM);
				}
			}
		}
	}
}


void DecodingDone( appledouble_decode_object* p_ap_decode_obj )
{
	FSSpec	fspec;
			
	fspec.vRefNum = p_ap_decode_obj->vRefNum;
	fspec.parID   = p_ap_decode_obj->dirId;
	fspec.name[0] = nsCRT::strlen(p_ap_decode_obj->fname);
	XP_STRCPY((char*)fspec.name+1, p_ap_decode_obj->fname);
	CMimeMapper * mapper = CPrefs::sMimeTypes.FindMimeType(fspec);
	if( mapper && (mapper->GetLoadAction() == CMimeMapper::Launch ) )
	{
		 LFileBufferStream file( fspec );
		 LaunchFile( &file );
	}	
}

#pragma cplusplus reset

/*
*	ap_encode_init
*	--------------
*	
*	Setup the encode envirment
*/

int ap_encode_init( appledouble_encode_object *p_ap_encode_obj, 
	                  char                      *fname,
                    char                      *separator)
{
	FSSpec	fspec;
	
	if (my_FSSpecFromPathname(fname, &fspec) != noErr )
		return -1;
	
  nsCRT::memset(p_ap_encode_obj, 0, sizeof(appledouble_encode_object));
	
	/*
	**	Fill out the source file inforamtion.
	*/	
	nsCRT::memcpy(p_ap_encode_obj->fname, fspec.name+1, *fspec.name);
	p_ap_encode_obj->fname[*fspec.name] = '\0';
	p_ap_encode_obj->vRefNum = fspec.vRefNum;
	p_ap_encode_obj->dirId   = fspec.parID;
	
	p_ap_encode_obj->boundary = nsCRT::strdup(separator);
	return noErr;
}
/*
**	ap_encode_next
**	--------------
**		
**		return :
**			noErr	:	everything is ok
**			errDone	:	when encoding is done.
**			errors	:	otherwise.
*/
int ap_encode_next(
	appledouble_encode_object* p_ap_encode_obj, 
	char 	*to_buff, 
	PRInt32 	buff_size, 
	PRInt32* 	real_size)
{
	int status;
	
	/*
	** 	install the out buff now.
	*/
	p_ap_encode_obj->outbuff     = to_buff;
	p_ap_encode_obj->s_outbuff 	 = buff_size;
	p_ap_encode_obj->pos_outbuff = 0;
	
	/*
	**	first copy the outstandind data in the overflow buff to the out buffer. 
	*/
	if (p_ap_encode_obj->s_overflow)
	{
		status = write_stream(p_ap_encode_obj, 
								p_ap_encode_obj->b_overflow,
								p_ap_encode_obj->s_overflow);
		if (status != noErr)
			return status;
				
		p_ap_encode_obj->s_overflow = 0;
	}

	/*
	** go the next processing stage based on the current state. 
	*/
	switch (p_ap_encode_obj->state)
	{
		case kInit:
			/*
			** We are in the  starting position, fill out the header.
			*/
			status = fill_apple_mime_header(p_ap_encode_obj); 
			if (status != noErr)
				break;					/* some error happens */
				
			p_ap_encode_obj->state = kDoingHeaderPortion;
			status = ap_encode_header(p_ap_encode_obj, true); 
										/* it is the first time to calling 		*/							
			if (status == errDone)
			{
				p_ap_encode_obj->state = kDoneHeaderPortion;
			}
			else
			{
				break;					/* we need more work on header portion.	*/
			}			
				
			/*
			** we are done with the header, so let's go to the data port.
			*/
			p_ap_encode_obj->state = kDoingDataPortion;
			status = ap_encode_data(p_ap_encode_obj, true);		 	
										/* it is first time call do data portion */
							
			if (status == errDone)
			{
				p_ap_encode_obj->state  = kDoneDataPortion;
				status = noErr;
			}
			break;

		case kDoingHeaderPortion:
		
			status = ap_encode_header(p_ap_encode_obj, false); 			
										/* continue with the header portion.	*/
			if (status == errDone)
			{
				p_ap_encode_obj->state = kDoneHeaderPortion;
			}
			else
			{
				break;					/* we need more work on header portion.	*/				
			}
			
			/*
			** start the data portion.
			*/
			p_ap_encode_obj->state = kDoingDataPortion;
			status = ap_encode_data(p_ap_encode_obj, true); 					
										/* it is the first time calling 		*/
			if (status == errDone)
			{
				p_ap_encode_obj->state  = kDoneDataPortion;
				status = noErr;
			}
			break;

		case kDoingDataPortion:
		
			status = ap_encode_data(p_ap_encode_obj, false); 				
										/* it is not the first time				*/
													
			if (status == errDone)
			{
				p_ap_encode_obj->state = kDoneDataPortion;
				status = noErr;
			}
			break;

		case kDoneDataPortion:
#if 0
			status = write_stream(p_ap_encode_obj,
									"\n-----\n\n",
									8);
			if (status == noErr)
#endif
				status = errDone;		/* we are really done.					*/

			break;
	}
	
	*real_size = p_ap_encode_obj->pos_outbuff;
	return status;
}

/*
**	ap_encode_end
**	-------------
**
**	clear the apple encoding.
*/

int ap_encode_end(
	appledouble_encode_object *p_ap_encode_obj, 
	PRBool is_aborting)
{
	/*
	** clear up the apple doubler.
	*/
	if (p_ap_encode_obj == NULL)
		return noErr;

	if (p_ap_encode_obj->fileId)			/* close the file if it is open.	*/
		FSClose(p_ap_encode_obj->fileId);

	PR_FREEIF(p_ap_encode_obj->boundary);		/* the boundary string.				*/
	
	return noErr;
}

#endif	/* the ifdef of XP_MAC */


/* 
** The initial of the apple double decoder.
**
**	 Set up the next output stream based on the input.
*/
int ap_decode_init(
	appledouble_decode_object* p_ap_decode_obj,
	PRBool	is_apple_single, 
	PRBool	write_as_binhex,
	void  	*closure)
{	
  nsCRT::memset(p_ap_decode_obj, 0, sizeof(appledouble_decode_object));
	
	/* presume first buff starts a line */
	p_ap_decode_obj->uu_starts_line = TRUE; 

	if (write_as_binhex)
	{
		p_ap_decode_obj->write_as_binhex = TRUE;
		p_ap_decode_obj->binhex_stream   = (NET_StreamClass*)closure;
		p_ap_decode_obj->data_size       = 0;
	}
	else
	{
		p_ap_decode_obj->write_as_binhex = FALSE;
		p_ap_decode_obj->binhex_stream   = NULL;
		
		p_ap_decode_obj->context = (MWContext*)closure;
	}
	
	p_ap_decode_obj->is_apple_single = is_apple_single;
	
	if (is_apple_single)
	{
		p_ap_decode_obj->encoding = kEncodeNone;
	}
	
	return NOERR;
}

static int ap_decode_state_machine(appledouble_decode_object* p_ap_decode_obj);
/*
*	process the buffer 
*/
int ap_decode_next(
	appledouble_decode_object* p_ap_decode_obj, 
	char 	*in_buff, 
	PRInt32 	buff_size)
{	
	/*
	** install the buff to the decoder.
	*/
	p_ap_decode_obj->inbuff   	= in_buff;
	p_ap_decode_obj->s_inbuff 	= buff_size;
	p_ap_decode_obj->pos_inbuff = 0;
	
	/*
	**	run off the decode state machine
	*/	
	return ap_decode_state_machine(p_ap_decode_obj);
}

PRIVATE int ap_decode_state_machine(
	appledouble_decode_object* p_ap_decode_obj)
{
	int 	status = NOERR;
	PRInt32 	size;
		
	switch (p_ap_decode_obj->state)
	{
		case kInit:
			/*
			**	Make sure that there are stuff in the buff 
			**		before we can parse the file head .
			*/
			if (p_ap_decode_obj->s_inbuff <=1 )
				return NOERR;
			
			if (p_ap_decode_obj->is_apple_single)
			{
				p_ap_decode_obj->state = kBeginHeaderPortion;
			}
			else
			{
				status = ap_seek_part_start(p_ap_decode_obj);
				if (status != errDone)
					return status;
	
				p_ap_decode_obj->state = kBeginParseHeader;
			}
			status = ap_decode_state_machine(p_ap_decode_obj);
			break;
		
		case kBeginSeekBoundary:
			p_ap_decode_obj->state = kSeekingBoundary;
			status = ap_seek_to_boundary(p_ap_decode_obj, TRUE);
			if (status == errDone)
			{
				p_ap_decode_obj->state = kBeginParseHeader;
				status = ap_decode_state_machine(p_ap_decode_obj);
			}	
			break;
			
		case kSeekingBoundary:
			status = ap_seek_to_boundary(p_ap_decode_obj, FALSE);
			if (status == errDone)
			{
				p_ap_decode_obj->state = kBeginParseHeader;
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
		
		case kBeginParseHeader:
			p_ap_decode_obj->state = kParsingHeader;
			status = ap_parse_header(p_ap_decode_obj, TRUE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->which_part == kDataPortion)
					p_ap_decode_obj->state = kBeginDataPortion;
				else if (p_ap_decode_obj->which_part == kHeaderPortion)
					p_ap_decode_obj->state = kBeginHeaderPortion;
				else
					p_ap_decode_obj->state = kFinishing;
	
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
				
		case kParsingHeader:
			status = ap_parse_header(p_ap_decode_obj, FALSE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->which_part == kDataPortion)
					p_ap_decode_obj->state = kBeginDataPortion;
				else if (p_ap_decode_obj->which_part == kHeaderPortion)
					p_ap_decode_obj->state = kBeginHeaderPortion;
				else
					p_ap_decode_obj->state = kFinishing;
										
				status = ap_decode_state_machine(p_ap_decode_obj);
			
			}
			break;
				
		case kBeginHeaderPortion:
			p_ap_decode_obj->state = kProcessingHeaderPortion;
			status = ap_decode_process_header(p_ap_decode_obj, TRUE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->is_apple_single)
					p_ap_decode_obj->state = kBeginDataPortion;
				else
					p_ap_decode_obj->state = kBeginSeekBoundary;
					
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
		case kProcessingHeaderPortion:
			status = ap_decode_process_header(p_ap_decode_obj, FALSE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->is_apple_single)
					p_ap_decode_obj->state = kBeginDataPortion;
				else
					p_ap_decode_obj->state = kBeginSeekBoundary;
					
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
		
		case kBeginDataPortion:
			p_ap_decode_obj->state = kProcessingDataPortion;
			status = ap_decode_process_data(p_ap_decode_obj, TRUE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->is_apple_single)
					p_ap_decode_obj->state = kFinishing;
				else
					p_ap_decode_obj->state = kBeginSeekBoundary;
					
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
		
		case kProcessingDataPortion:
			status = ap_decode_process_data(p_ap_decode_obj, FALSE);
			if (status == errDone)
			{
				if (p_ap_decode_obj->is_apple_single)
					p_ap_decode_obj->state = kFinishing;
				else
					p_ap_decode_obj->state = kBeginSeekBoundary;
		
				status = ap_decode_state_machine(p_ap_decode_obj);
			}
			break;
			
		case kFinishing:
			if (p_ap_decode_obj->write_as_binhex)
			{
				if (p_ap_decode_obj->tmpfd)
				{
					/*
					**	It is time to append the data fork to bin hex encoder.
					**	
					**	The reason behind this dirt work is resource fork is the last
					**	piece in the binhex, while it is the first piece in apple double. 
					*/
					XP_FileSeek(p_ap_decode_obj->tmpfd, 0L, SEEK_SET);
					
					while (p_ap_decode_obj->data_size > 0)
					{
						char buff[1024];
					
						size = PR_MIN(1024, p_ap_decode_obj->data_size);
						XP_FileRead(buff, size, p_ap_decode_obj->tmpfd);
					
						status = (*p_ap_decode_obj->binhex_stream->put_block)
									(p_ap_decode_obj->binhex_stream->data_object, 
									buff, 
									size);
						
						p_ap_decode_obj->data_size -= size;
					}
				}
				
				if (p_ap_decode_obj->data_size <= 0)
				{
					/* CALL put_block with size == 0 to close a part. */
					status = (*p_ap_decode_obj->binhex_stream->put_block)
								(p_ap_decode_obj->binhex_stream->data_object, 
								NULL, 
								0);
					if (status != NOERR)
						break;
											
					/* and now we are really done.					*/
					status = errDone;
				}
				else
					status = NOERR;
			}
			break;
	}						
	return (status == errEOB) ? NOERR : status;	
}

int ap_decode_end(
	appledouble_decode_object* p_ap_decode_obj, 
	PRBool 	is_aborting)
{
	/*
	** clear up the apple doubler object.
	*/
	if (p_ap_decode_obj == NULL)
		return NOERR;
		
	PR_FREEIF(p_ap_decode_obj->boundary0);

#ifdef	XP_MAC
	if (p_ap_decode_obj->fileId)
		FSClose(p_ap_decode_obj->fileId);
	if( p_ap_decode_obj->vRefNum )
		FlushVol(nil, p_ap_decode_obj->vRefNum );
#endif

	if (p_ap_decode_obj->write_as_binhex)
	{
		/*		
		** make sure close the binhex stream too. 
		*/
		if (is_aborting)
		{
			(*p_ap_decode_obj->binhex_stream->abort)
				(p_ap_decode_obj->binhex_stream->data_object, 0);		
		}
		else
		{
			(*p_ap_decode_obj->binhex_stream->complete)
				(p_ap_decode_obj->binhex_stream->data_object);		
		}

		if (p_ap_decode_obj->tmpfd)
			XP_FileClose(p_ap_decode_obj->tmpfd);
		
		if (p_ap_decode_obj->tmpfname)
		{
			XP_FileRemove(p_ap_decode_obj->tmpfname, xpTemporary);		
														/* remove tmp file if we used it	*/	
			PR_FREEIF(p_ap_decode_obj->tmpfname);			/* and release the file name too.	*/
		}
	}
	else if (p_ap_decode_obj->fd)
	{
		XP_FileClose(p_ap_decode_obj->fd);
	}
#ifdef XP_MAC
	if( !is_aborting )
		DecodingDone( p_ap_decode_obj);
#endif
	return NOERR;

}
