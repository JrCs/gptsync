/*
 * gptsync/gptsync.c
 * Platform-independent code for syncing GPT and MBR
 *
 * Copyright (c) 2006-2007 Christoph Pfisterer
 * All rights reserved.
 *
 * Enhanced version by JrCs 2009-2013
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include "gptsync.h"

// #include "syslinux_mbr.h"

// Converts a hexadecimal string to integer
// Return:
//	0    - Conversion is successful
//	1    - String is empty
//	2    - String has more than 2 bytes
//	4    - Conversion is in process but abnormally terminated by
//         illegal hexadecimal character
int xtoi(const char* xs, UINT8* result)
{
	size_t szlen = strlen(xs);
	int i, xv, fact;
	
	if (szlen > 0)
	{
		// Converting more than 8bit hexadecimal value?
		if (szlen>2) return 2; // exit
		
		// Begin conversion here
		*result = 0;
		fact = 1;
		
		// Run until no more character to convert
		for(i=szlen-1; i>=0 ;i--)
		{
			if (isxdigit(*(xs+i)))
			{
				if (*(xs+i)>=97)
				{
					xv = ( *(xs+i) - 97) + 10;
				}
				else if ( *(xs+i) >= 65)
				{
					xv = (*(xs+i) - 65) + 10;
				}
				else
				{
					xv = *(xs+i) - 48;
				}
				*result += (xv * fact);
				fact *= 16;
			}
			else
			{
				// Conversion was abnormally terminated
				// by non hexadecimal digit, hence
				// returning only the converted with
				// an error value 4 (illegal hex character)
				return 4;
			}
		}
		return 0;
	}
	
	// Nothing to convert
	return 1;
}

//
// MBR functions
//

static UINTN check_mbr(VOID)
{
    UINTN       i;
    
    // check each entry
    for (i = 0; i < mbr_part_count; i++) {
        /*
		// check for overlap
        for (k = 0; k < mbr_part_count; k++) {
            if (k != i && !(mbr_parts[i].start_lba > mbr_parts[k].end_lba || mbr_parts[k].start_lba > mbr_parts[i].end_lba)) {
                Print(L"Status: MBR partition table is invalid, partitions overlap.\n");
                return 1;
            }
        }
		*/
        
        // check for extended partitions
        if (mbr_parts[i].mbr_type == 0x05 || mbr_parts[i].mbr_type == 0x0f || mbr_parts[i].mbr_type == 0x85) {
            Print(L"Status: Extended partition found in MBR table, will not touch this disk.\n",
                  gpt_parts[i].gpt_parttype->name);
            return 1;
        }
    }
    
    return 0;
}

static UINTN write_mbr(VOID)
{
    UINTN               status;
    UINTN               i, k;
    UINT8               active;
    UINT64              lba;
    MBR_PARTITION_INFO  *table;
    
    Print(L"\nWriting new MBR...\n");
    
    // read MBR data
    status = read_sector(0, sector);
    if (status != 0)
        return status;
    
    // write partition table
    *((UINT16 *)(sector + 510)) = 0xaa55;
    
    table = (MBR_PARTITION_INFO *)(sector + 446);
    active = 0x80;
    for (i = 0; i < 4; i++) {
        for (k = 0; k < new_mbr_part_count; k++) {
            if (new_mbr_parts[k].index == i)
                break;
        }
        if (k >= new_mbr_part_count) {
            // unused entry
            table[i].flags        = 0;
            table[i].start_chs[0] = 0;
            table[i].start_chs[1] = 0;
            table[i].start_chs[2] = 0;
            table[i].type         = 0;
            table[i].end_chs[0]   = 0;
            table[i].end_chs[1]   = 0;
            table[i].end_chs[2]   = 0;
            table[i].start_lba    = 0;
            table[i].size         = 0;
        } else {
            if (new_mbr_parts[k].active) {
                table[i].flags        = active;
                active = 0x00;
            } else
                table[i].flags        = 0x00;
            table[i].start_chs[0] = 0xfe;
            table[i].start_chs[1] = 0xff;
            table[i].start_chs[2] = 0xff;
            table[i].type         = new_mbr_parts[k].mbr_type;
            table[i].end_chs[0]   = 0xfe;
            table[i].end_chs[1]   = 0xff;
            table[i].end_chs[2]   = 0xff;
            
            lba = new_mbr_parts[k].start_lba;
            if (lba > 0xffffffffULL) {
                Print(L"Warning: Partition %d starts beyond 2 TiB limit\n", i+1);
                lba = 0xffffffffULL;
            }
            table[i].start_lba    = (UINT32)lba;
            
            lba = new_mbr_parts[k].end_lba + 1 - new_mbr_parts[k].start_lba;
            if (lba > 0xffffffffULL) {
                Print(L"Warning: Partition %d extends beyond 2 TiB limit\n", i+1);
                lba = 0xffffffffULL;
            }
            table[i].size         = (UINT32)lba;
        }
    }
    
    // write MBR data
    status = write_sector(0, sector);
    if (status != 0)
        return status;
    
    Print(L"MBR updated successfully!\n");
    
    return 0;
}

//
// GPT functions
//

static UINTN check_gpt(VOID)
{
    UINTN       i, k;
    BOOLEAN     found_data_parts;
    
    if (gpt_part_count == 0) {
        Print(L"Status: No GPT partition table, no need to sync.\n");
        return 1;
    }
    
    // check each entry
    found_data_parts = FALSE;
    for (i = 0; i < gpt_part_count; i++) {
        // check sanity
        if (gpt_parts[i].end_lba < gpt_parts[i].start_lba) {
            Print(L"Status: GPT partition table is invalid.\n");
            return 1;
        }
        // check for overlap
        for (k = 0; k < gpt_part_count; k++) {
            if (k != i && !(gpt_parts[i].start_lba > gpt_parts[k].end_lba || gpt_parts[k].start_lba > gpt_parts[i].end_lba)) {
                Print(L"Status: GPT partition table is invalid, partitions overlap.\n");
                return 1;
            }
        }
        
        // check for partitions kind
        if (gpt_parts[i].gpt_parttype->kind == GPT_KIND_FATAL) {
            Print(L"Status: GPT partition of type '%s' found, will not touch this disk.\n",
                  gpt_parts[i].gpt_parttype->name);
            return 1;
        }
        if (gpt_parts[i].gpt_parttype->kind == GPT_KIND_DATA ||
            gpt_parts[i].gpt_parttype->kind == GPT_KIND_BASIC_DATA)
            found_data_parts = TRUE;
    }
    
    if (!found_data_parts) {
        Print(L"Status: GPT partition table has no data partitions, no need to sync.\n");
        return 1;
    }
    
    return 0;
}

static void add_gpt_partition_to_mbr(int mbr_part_index, int gpt_part_index, UINT8 force_type, BOOLEAN active) {
	int k;
	
	new_mbr_parts[mbr_part_index].index     = mbr_part_index;
	new_mbr_parts[mbr_part_index].start_lba = gpt_parts[gpt_part_index].start_lba;
	new_mbr_parts[mbr_part_index].end_lba   = gpt_parts[gpt_part_index].end_lba;
	new_mbr_parts[mbr_part_index].mbr_type  = force_type ? force_type : gpt_parts[gpt_part_index].mbr_type;
	new_mbr_parts[mbr_part_index].active    = active;

	// find matching partition in the old MBR table
	for (k = 0; k < mbr_part_count; k++) {
		if (mbr_parts[k].start_lba == gpt_parts[gpt_part_index].start_lba) {
			// keep type if not detected
			if (new_mbr_parts[mbr_part_index].mbr_type == 0)
				new_mbr_parts[mbr_part_index].mbr_type = mbr_parts[k].mbr_type;
			break;
		}
	}
	
	if (new_mbr_parts[mbr_part_index].mbr_type == 0)
		// final fallback: set to a (hopefully) unused type
		new_mbr_parts[mbr_part_index].mbr_type = 0xc0;
}

//
// compare GPT and MBR tables
//

#define ACTION_NOP         (0)
#define ACTION_REWRITE     (1)

static UINTN analyze(int optind, int argc, char **argv)
{
    UINTN   action;
    UINTN   i, k, count_active, detected_parttype;
    CHARN   *fsname;
    UINT64  min_start_lba, max_end_lba, block_count, last_disk_lba;
    UINTN   status;
    BOOLEAN have_esp;
    
    new_mbr_part_count = 0;
    
	block_count = get_disk_size();
	last_disk_lba = block_count - 1;
	if (block_count == 0) {
		error("can't retrieve disk size");
		return 1;
	}

    // determine correct MBR types for GPT partitions
    if (gpt_part_count == 0) {
        Print(L"Status: No GPT partitions defined, nothing to sync.\n");
        return 1;
    }
    have_esp = FALSE;
    for (i = 0; i < gpt_part_count; i++) {
        gpt_parts[i].mbr_type = gpt_parts[i].gpt_parttype->mbr_type;
        if (gpt_parts[i].gpt_parttype->kind == GPT_KIND_BASIC_DATA) {
            // Basic Data: need to look at data in the partition
            status = detect_mbrtype_fs(gpt_parts[i].start_lba, &detected_parttype, &fsname);
            if (detected_parttype)
                gpt_parts[i].mbr_type = detected_parttype;
            else
                gpt_parts[i].mbr_type = 0x0b;  // fallback: FAT32
        } else if (gpt_parts[i].mbr_type == 0xef) {
            // EFI System Partition: GNU parted can put this on any partition,
            // need to detect file systems
            status = detect_mbrtype_fs(gpt_parts[i].start_lba, &detected_parttype, &fsname);
            if (!have_esp && (detected_parttype == 0x01 || detected_parttype == 0x0e || detected_parttype == 0x0c))
                ;  // seems to be a legitimate ESP, don't change
            else if (detected_parttype)
                gpt_parts[i].mbr_type = detected_parttype;
            else if (have_esp)    // make sure there's no more than one ESP per disk
                gpt_parts[i].mbr_type = 0x83;  // fallback: Linux
        }
        // NOTE: mbr_type may still be 0 if content detection fails for exotic GPT types or file systems
        
        if (gpt_parts[i].mbr_type == 0xef)
            have_esp = TRUE;
    }
    
    // generate the new table
    
	new_mbr_part_count = 1;
	count_active = 0;
	
	if (! create_empty_mbr) {
		if (optind < argc) {
			for (i = optind; i < argc; i++) {
				char *separator, csep = 0;
				BOOLEAN active   = FALSE;
				UINT8 force_type = 0;
				separator = strchr (argv[i], '+');
				if (! separator)
					separator = strchr (argv[i], '-');
				if (separator)
				{
					csep = *separator;
					*separator = 0;
				}
			
				int part = atoi(argv[i]);
				if (part < 1 || part > gpt_part_count) {
					error("invalid argument '%s', partition number must be between 1-%d !", argv[i], gpt_part_count);
					return 1;
				}
				part--; // 0 base partition number
				if (csep == '+') {
					active = TRUE;
					count_active ++;
					if (count_active == 2) {
						error("only one partition can be active !");
						return 1;
					}
				}

				if (separator && *(separator + 1)) {
					char *str_type = separator + 1;
					if(xtoi (str_type, &force_type) != 0) {
						error("invalid hex type: %s !", str_type);
						return 1;
					}
				}
			
				// Check that a partition has not already enter
				for (k = 1; k < new_mbr_part_count; k++) {
					if (new_mbr_parts[k].start_lba == gpt_parts[part].start_lba ||
						new_mbr_parts[k].end_lba   == gpt_parts[part].end_lba ) {
							error("you already add partition %d !",part+1);
							return 1;
					}
				}
			
				add_gpt_partition_to_mbr(new_mbr_part_count, part, force_type, active);
				new_mbr_part_count++;
			}
		}
		else {
			// add other GPT partitions until the table is full
			// TODO: in the future, prioritize partitions by kind
			if (gpt_parts[0].mbr_type == 0xef)
				i = 1;
			else
				i = 0;
		
			for (; i < gpt_part_count && new_mbr_part_count < 4; i++) {
				add_gpt_partition_to_mbr(new_mbr_part_count, i, 0, FALSE);
        
				new_mbr_part_count++;
			}
		}
	}
	
	// get the first and last used lba
	if ( new_mbr_part_count == 1) { // Only one EFI Protective partition
		min_start_lba = max_end_lba  = last_disk_lba + 1; // Take whole disk
	}
	else {
		min_start_lba = new_mbr_parts[1].start_lba;
		max_end_lba   = new_mbr_parts[1].end_lba;
		for (k = 2; k < new_mbr_part_count; k++) {
			if (max_end_lba < new_mbr_parts[k].end_lba)
				max_end_lba = new_mbr_parts[k].end_lba;
			if (min_start_lba > new_mbr_parts[k].start_lba)
				min_start_lba = new_mbr_parts[k].start_lba;
		}
	}
	
	// Reserved last part if not used
	if (new_mbr_part_count < 4 && max_end_lba < last_disk_lba && fill_mbr) {
		new_mbr_parts[new_mbr_part_count].index = new_mbr_part_count;
		new_mbr_parts[new_mbr_part_count].start_lba = max_end_lba + 1;
		new_mbr_parts[new_mbr_part_count].end_lba   = last_disk_lba;
		new_mbr_parts[new_mbr_part_count].mbr_type  = 0xee; // another protective area
		new_mbr_parts[new_mbr_part_count].active    = FALSE;
		new_mbr_part_count ++;
	}
    
    // first entry: EFI Protective
    new_mbr_parts[0].index     = 0;
    new_mbr_parts[0].start_lba = 1;
    new_mbr_parts[0].end_lba   = min_start_lba - 1;
    new_mbr_parts[0].mbr_type  = 0xee;
        
	action = ACTION_NOP;

	// Check if we need to rewrite MBR
	for (i = 0; i < 4; i ++) {
		if (new_mbr_parts[i].index     != mbr_parts[i].index     ||
			new_mbr_parts[i].start_lba != mbr_parts[i].start_lba ||
			new_mbr_parts[i].end_lba   != mbr_parts[i].end_lba   ||
			new_mbr_parts[i].mbr_type  != mbr_parts[i].mbr_type  ||
			new_mbr_parts[i].active    != mbr_parts[i].active) {
			action = ACTION_REWRITE;
			break;
		}
	}
	
    if (action == ACTION_NOP) {
        Print(L"Status: Tables are synchronized, no need to sync.\n");
        return 1;
    }
	else {
        Print(L"Status: MBR table must be updated.\n");
    }
    
    // dump table
    Print(L"\nProposed new MBR partition table:\n");
    Print(L" # A    Start LBA      End LBA  Type\n");
    for (i = 0; i < new_mbr_part_count; i++) {
        Print(L" %d %s %12lld %12lld  %02x  %s\n",
              new_mbr_parts[i].index + 1,
              new_mbr_parts[i].active ? STR("*") : STR(" "),
              new_mbr_parts[i].start_lba,
              new_mbr_parts[i].end_lba,
              new_mbr_parts[i].mbr_type,
              mbr_parttype_name(new_mbr_parts[i].mbr_type));
    }
    
    return 0;
}

//
// sync algorithm entry point
//

UINTN gptsync(int optind, int argc, char **argv)
{
    UINTN   status = 0;
    UINTN   status_gpt, status_mbr;
    BOOLEAN proceed = FALSE;
    
    // get full information from disk
    status_gpt = read_gpt();
    status_mbr = read_mbr();
    if (status_gpt != 0 || status_mbr != 0)
        return (status_gpt || status_mbr);
    
    // cross-check current situation
    Print(L"\n");
    status = check_gpt();   // check GPT for consistency
    if (status != 0)
        return status;
    status = check_mbr();   // check MBR for consistency
    if (status != 0)
        return status;
    status = analyze(optind, argc, argv);     // analyze the situation & compose new MBR table
    if (status != 0)
        return status;

    // offer user the choice what to do
    status = input_boolean(STR("\nMay I update the MBR as printed above? [y/N] "), &proceed);
    if (status != 0 || proceed != TRUE)
        return status;
    
    // adjust the MBR and write it back
    status = write_mbr();
    if (status != 0)
        return status;
    
    return status;
}
