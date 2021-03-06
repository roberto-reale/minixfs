/* truncate.c - automatically generated from truncate.c.in */
/* by function-tailoring.sh on Sat Apr 19 15:30:47 UTC 2014 */

#line 1 "truncate.c.in"
/* File truncation -*-C-*- 

   NOTE: This file is processed by `function-tailoring.sh' to get
   `truncate.c'.

   Copyright (C) 1995,96,97,99,2000 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

   Modified for the MINIX file system from the original for the ext2
   file system by Roberto Reale <rober.reale@gmail.com>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "minixfs.h"

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

/* ---------------------------------------------------------------- */

/* A sequence of zones to be freed in NODE.  */
struct free_zone_run
{
  zone_t first_zone;
  unsigned long num_zones;
  struct node *node;
};

/* Initialize FZR, pointing to NODE.  */
static inline void
free_zone_run_init (struct free_zone_run *fzr, struct node *node)
{
  fzr->num_zones = 0;
  fzr->node = node;
}

static inline void
_free_zone_run_flush (struct free_zone_run *fzr, unsigned long count)
{
  fzr->node->dn_stat.st_blocks -= count <<
    (log2_stat_blocks_per_fs_block + log2_fs_blocks_per_zone);
  fzr->node->dn_stat_dirty = 1;

  minixfs_debug ("flushing freed zones %u-%u (node %Ld)", fzr->first_zone,
		 (zone_t) (fzr->first_zone + count), fzr->node->cache_id);
  minixfs_free_zones (fzr->first_zone, count);
}

/* Add ZONE to the list of zones to be freed in FZR.  */
static inline void
free_zone_run_add (struct free_zone_run *fzr, zone_t zone)
{
  unsigned long count = fzr->num_zones;

  assert (zone >= sblock->s_firstdatazone && zone < sblock_info->s_nzones);
  if (count == 0)
    {
      fzr->first_zone = zone;
      fzr->num_zones++;
    }
  else if (count > 0 && fzr->first_zone == zone - count)
    fzr->num_zones++;
  else
    {
      _free_zone_run_flush (fzr, count);
      fzr->first_zone = zone;
      fzr->num_zones = 1;
    }
}

/* If *P is non-zero, set it to zero, and add the zone it pointed to the
   list of zones to be freed in FZR.  */
static inline void
free_zone_run_free_ptr (struct free_zone_run *fzr, zone_t *p)
{
  zone_t zone = *p;
  if (zone)
    {
      *p = 0;
      free_zone_run_add (fzr, zone);
    }
}

/* Free any zones left in FZR, and cleanup any resources it's using.  */
static inline void
free_zone_run_finish (struct free_zone_run *fzr)
{
  unsigned long count = fzr->num_zones;
  if (count > 0)
    _free_zone_run_flush (fzr, count);
}

/* ---------------------------------------------------------------- */

#line 111 "truncate.c.in"

/* Free any direct zones starting with zone at node index END.  */
static void
trunc_direct_V1 (struct node *node, zone_t end,
 struct free_zone_run *fzr)
{
  uint16_t *zones =
    (uint16_t *) (node->dn->info.i_zone_V1);

  minixfs_debug ("truncating direct zones starting at node index %u "
 "(node %Ld)", end, node->cache_id);

  while (end < MINIXFS_NDIR_ZONES)
    free_zone_run_free_ptr (fzr, (zone_t *) (zones + (int) end++));
}

/* Free any zones in NODE greater than or equal to END that are rooted
   in the block of indirection contained in the zone *P; OFFSET should
   be the zone position that *P corresponds to.  For each zone pointer
   in *P that should be freed, FREE_ZONE is called with a pointer to
   the entry for that zone, and the index of the entry within *P.  If
   every zone in *P is freed, then *P is set to 0, otherwise it is
   left alone.  */
static void
trunc_indirect_V1 (struct node *node, uint16_t end,
   uint16_t *p, uint16_t offset,
   void (*free_zone)(uint16_t *p,
     unsigned index),
   struct free_zone_run *fzr)
{
  if (*p)
    {
      unsigned index;
      int modified = 0, all_freed = 1;
      uint16_t *ind_bh = (uint16_t *) zptr ((zone_t) *p);
      unsigned first = end < offset ? 0 : end - offset;

      /* XXX only the first block in a zone does contain pointers */
      for (index = first; index < ADDR_PER_BLOCK; index++)
if (ind_bh[index])
  {
    (*free_zone)(ind_bh + index, index);
    if (ind_bh[index])
      all_freed = 0;/* Some descendent hasn't been freed.  */
    else
      modified = 1;
  }

      if (first == 0 && all_freed)
{
  pager_flush_some (diskfs_disk_pager, zoffs ((zone_t) *p),
    zone_size, 1);
  free_zone_run_free_ptr (fzr, (zone_t *) p);
}
      else if (modified)
record_indir_poke (node, (char *) ind_bh);
    }
}

static void
trunc_single_indirect_V1 (struct node *node, uint16_t end,
  uint16_t *p, uint16_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint16_t *p, unsigned index)
    {
      free_zone_run_free_ptr (fzr, (zone_t *) p);
    }
  trunc_indirect_V1 (node, end, p, offset, free_zone, fzr);
}

static void
trunc_double_indirect_V1 (struct node *node, uint16_t end,
  uint16_t *p, uint16_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint16_t *p, unsigned index)
    {
      uint16_t entry_offs = offset + (index * ADDR_PER_BLOCK);
      trunc_single_indirect_V1 (node, end, p, entry_offs, fzr);
    }
  trunc_indirect_V1 (node, end, p, offset, free_zone, fzr);
}

static void
trunc_triple_indirect_V1 (struct node *node, uint16_t end,
  uint16_t *p, uint16_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint16_t *p, unsigned index)
    {
      zone_t entry_offs =
offset + (index * ADDR_PER_BLOCK * ADDR_PER_BLOCK);
      trunc_double_indirect_V1 (node, end, p, entry_offs, fzr);
    }
  trunc_indirect_V1 (node, end, p, offset, free_zone, fzr);
}


#line 111 "truncate.c.in"

/* Free any direct zones starting with zone at node index END.  */
static void
trunc_direct_V2 (struct node *node, zone_t end,
 struct free_zone_run *fzr)
{
  uint32_t *zones =
    (uint32_t *) (node->dn->info.i_zone_V2);

  minixfs_debug ("truncating direct zones starting at node index %u "
 "(node %Ld)", end, node->cache_id);

  while (end < MINIXFS_NDIR_ZONES)
    free_zone_run_free_ptr (fzr, (zone_t *) (zones + (int) end++));
}

/* Free any zones in NODE greater than or equal to END that are rooted
   in the block of indirection contained in the zone *P; OFFSET should
   be the zone position that *P corresponds to.  For each zone pointer
   in *P that should be freed, FREE_ZONE is called with a pointer to
   the entry for that zone, and the index of the entry within *P.  If
   every zone in *P is freed, then *P is set to 0, otherwise it is
   left alone.  */
static void
trunc_indirect_V2 (struct node *node, uint32_t end,
   uint32_t *p, uint32_t offset,
   void (*free_zone)(uint32_t *p,
     unsigned index),
   struct free_zone_run *fzr)
{
  if (*p)
    {
      unsigned index;
      int modified = 0, all_freed = 1;
      uint32_t *ind_bh = (uint32_t *) zptr ((zone_t) *p);
      unsigned first = end < offset ? 0 : end - offset;

      /* XXX only the first block in a zone does contain pointers */
      for (index = first; index < ADDR_PER_BLOCK; index++)
if (ind_bh[index])
  {
    (*free_zone)(ind_bh + index, index);
    if (ind_bh[index])
      all_freed = 0;/* Some descendent hasn't been freed.  */
    else
      modified = 1;
  }

      if (first == 0 && all_freed)
{
  pager_flush_some (diskfs_disk_pager, zoffs ((zone_t) *p),
    zone_size, 1);
  free_zone_run_free_ptr (fzr, (zone_t *) p);
}
      else if (modified)
record_indir_poke (node, (char *) ind_bh);
    }
}

static void
trunc_single_indirect_V2 (struct node *node, uint32_t end,
  uint32_t *p, uint32_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint32_t *p, unsigned index)
    {
      free_zone_run_free_ptr (fzr, (zone_t *) p);
    }
  trunc_indirect_V2 (node, end, p, offset, free_zone, fzr);
}

static void
trunc_double_indirect_V2 (struct node *node, uint32_t end,
  uint32_t *p, uint32_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint32_t *p, unsigned index)
    {
      uint32_t entry_offs = offset + (index * ADDR_PER_BLOCK);
      trunc_single_indirect_V2 (node, end, p, entry_offs, fzr);
    }
  trunc_indirect_V2 (node, end, p, offset, free_zone, fzr);
}

static void
trunc_triple_indirect_V2 (struct node *node, uint32_t end,
  uint32_t *p, uint32_t offset,
  struct free_zone_run *fzr)
{
  void free_zone (uint32_t *p, unsigned index)
    {
      zone_t entry_offs =
offset + (index * ADDR_PER_BLOCK * ADDR_PER_BLOCK);
      trunc_double_indirect_V2 (node, end, p, entry_offs, fzr);
    }
  trunc_indirect_V2 (node, end, p, offset, free_zone, fzr);
}



/* ---------------------------------------------------------------- */

/* Write something to each page from START to END inclusive of memory
   object OBJ, but make sure the data doesns't actually change.  */
static void
poke_pages (memory_object_t obj, vm_offset_t start, vm_offset_t end)
{
  while (start < end)
    {
      error_t err;
      vm_size_t len = 8 * vm_page_size;
      vm_address_t addr = 0;

      if (len > end - start)
	len = end - start;

      err = vm_map (mach_task_self (), &addr, len, 0, 1, obj, start, 0,
		    VM_PROT_WRITE|VM_PROT_READ, VM_PROT_READ|VM_PROT_WRITE, 0);
      if (!err)
	{
	  vm_address_t poke;
	  for (poke = addr; poke < addr + len; poke += vm_page_size)
	    *(volatile int *)poke = *(volatile int *)poke;
	  munmap ((caddr_t) addr, len);
	}

      start += len;
    }
}

/* Flush all the data past the new size from the kernel.  Also force any
   delayed copies of this data to take place immediately.  (We are implicitly
   changing the data to zeros and doing it without the kernel's immediate
   knowledge; accordingly we must help out the kernel thusly.)  */
static void
force_delayed_copies (struct node *node, off_t length)
{
  struct pager *pager;

  spin_lock (&node_to_page_lock);
  pager = node->dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (pager)
    {
      mach_port_t obj;

      pager_change_attributes (pager, MAY_CACHE, MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (node, VM_PROT_READ);
      if (obj != MACH_PORT_NULL)
	{
	  /* XXX should cope with errors from diskfs_get_filemap */
	  poke_pages (obj, round_page (length), round_page (node->allocsize));
	  mach_port_deallocate (mach_task_self (), obj);
	  pager_flush_some (pager, round_page(length),
			    node->allocsize - length, 1);
	}

      ports_port_deref (pager);
    }
}

static void
enable_delayed_copies (struct node *node)
{
  struct pager *pager;

  spin_lock (&node_to_page_lock);
  pager = node->dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (pager)
    {
      pager_change_attributes (pager, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY, 0);
      ports_port_deref (pager);
    }
}

/* ---------------------------------------------------------------- */

#line 296 "truncate.c.in"
static void
minixfs_truncate_V1 (struct node *node,
     struct free_zone_run *fzr, off_t length)
{
  uint16_t offs;
  uint16_t end = (uint16_t) boffs_zone (round_zone (length));
  uint16_t *zptrs =
    (uint16_t *) node->dn->info.i_zone_V1;

  trunc_direct_V1 (node, end, fzr);

  offs = MINIXFS_NDIR_ZONES;
  trunc_single_indirect_V1 (node, end, zptrs + MINIXFS_IND_ZONE,
    offs, fzr);
  if (sblock_info->s_has_dind)
    {
      offs += ADDR_PER_BLOCK;
      trunc_double_indirect_V1 (node, end, zptrs + MINIXFS_DIND_ZONE,
offs, fzr);
      if (sblock_info->s_has_tind)
{
  offs += ADDR_PER_BLOCK * ADDR_PER_BLOCK;
  trunc_triple_indirect_V1 (node, end,
    zptrs + MINIXFS_TIND_ZONE,
    offs, fzr);
}
    }
}

#line 296 "truncate.c.in"
static void
minixfs_truncate_V2 (struct node *node,
     struct free_zone_run *fzr, off_t length)
{
  uint32_t offs;
  uint32_t end = (uint32_t) boffs_zone (round_zone (length));
  uint32_t *zptrs =
    (uint32_t *) node->dn->info.i_zone_V2;

  trunc_direct_V2 (node, end, fzr);

  offs = MINIXFS_NDIR_ZONES;
  trunc_single_indirect_V2 (node, end, zptrs + MINIXFS_IND_ZONE,
    offs, fzr);
  if (sblock_info->s_has_dind)
    {
      offs += ADDR_PER_BLOCK;
      trunc_double_indirect_V2 (node, end, zptrs + MINIXFS_DIND_ZONE,
offs, fzr);
      if (sblock_info->s_has_tind)
{
  offs += ADDR_PER_BLOCK * ADDR_PER_BLOCK;
  trunc_triple_indirect_V2 (node, end,
    zptrs + MINIXFS_TIND_ZONE,
    offs, fzr);
}
    }
}


/* The user must define this function.  Truncate locked node NODE to
   be SIZE bytes long.  (If NODE is already less than or equal to SIZE
   bytes long, do nothing.)  If this is a symlink (and
   diskfs_shortcut_symlink is set) then this should clear the symlink,
   even if diskfs_create_symlink_hook stores the link target
   elsewhere.  */
error_t
diskfs_truncate (struct node *node, off_t length)
{
  error_t err;
  off_t offset;

  diskfs_check_readonly ();
  assert (!diskfs_readonly);

  if (length >= node->dn_stat.st_size)
    return 0;

  if (! node->dn_stat.st_blocks)
    /* There aren't really any blocks allocated, so just frob the size.  This
       is true for fast symlinks, and also apparently for some device nodes
       in linux.  */
    {
      node->dn_stat.st_size = length;
      node->dn_set_mtime = 1;
      node->dn_set_ctime = 1;
      diskfs_node_update (node, 1);
      return 0;
    }

  /*
   * If the file is not being truncated to a zone boundary, the
   * contents of the partial zone following the end of the file must be
   * zeroed in case it ever becomes accessible again because of
   * subsequent file growth.
   */
  offset = length & (zone_size - 1);
  if (offset > 0)
    {
      diskfs_node_rdwr (node, (void *) zerozone, length, zone_size - offset,
			1, 0, 0);
      diskfs_file_update (node, 1);
    }

  force_delayed_copies (node, length);

  rwlock_writer_lock (&node->dn->alloc_lock);

  /* Update the size on disk; fsck will finish freeing blocks if necessary
     should we crash.  */
  node->dn_stat.st_size = length;
  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  diskfs_node_update (node, 1);

  err = diskfs_catch_exception ();
  if (!err)
    {
      struct free_zone_run fzr;
      free_zone_run_init (&fzr, node);

      if (sblock_info->s_version == MINIX_V1)
	minixfs_truncate_V1 (node, &fzr, length);
      else
	minixfs_truncate_V2 (node, &fzr, length);

      free_zone_run_finish (&fzr);

      node->allocsize = round_block (length);

      /* Set our last_page_partially_writable to a pessimistic state -- it
	 won't hurt if is wrong.  */
      node->dn->last_page_partially_writable =
	trunc_page (node->allocsize) != node->allocsize;

      diskfs_end_catch_exception ();
    }

  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  node->dn_stat_dirty = 1;

  /* Now we can permit delayed copies again.  */
  enable_delayed_copies (node);

  rwlock_writer_unlock (&node->dn->alloc_lock);

  return err;
}
