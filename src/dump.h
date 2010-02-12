/*
    $Id$

    FLV Metadata updater

    Copyright (C) 2007-2010 Marc Noirot <marc.noirot AT gmail.com>

    This file is part of FLVMeta.

    FLVMeta is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    FLVMeta is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FLVMeta; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#ifndef __DUMP_H__
#define __DUMP_H__

#include "flvmeta.h"

/* dump metadata from a FLV file */
extern int dump_metadata(const flvmeta_opts * options);

/* dump the full contents of an FLV file */
extern int dump_flv_file(const flvmeta_opts * options);

/* dump AMF data directly */
extern int dump_amf_data(const amf_data * data, const flvmeta_opts * options);

#endif /* __DUMP_H__ */
