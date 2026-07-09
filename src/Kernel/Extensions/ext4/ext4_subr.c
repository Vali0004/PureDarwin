/*
 * ext4_subr.c - superblock, inode, extent, and block I/O helpers
 */
#include "ext4.h"
#include <sys/buf.h>
#include <sys/ubc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <string.h>

enum vtype
ext4_ft_to_vtype(uint8_t ft)
{
	switch (ft) {
	case EXT4_FT_REG_FILE: return VREG;
	case EXT4_FT_DIR:      return VDIR;
	case EXT4_FT_CHRDEV:   return VCHR;
	case EXT4_FT_BLKDEV:   return VBLK;
	case EXT4_FT_FIFO:     return VFIFO;
	case EXT4_FT_SOCK:     return VSOCK;
	case EXT4_FT_SYMLINK:  return VLNK;
	default:              return VNON;
	}
}

enum vtype
ext4_mode_to_vtype(uint16_t mode)
{
	switch (mode & EXT4_S_IFMT) {
	case EXT4_S_IFREG:  return VREG;
	case EXT4_S_IFDIR:  return VDIR;
	case EXT4_S_IFCHR:  return VCHR;
	case EXT4_S_IFBLK:  return VBLK;
	case EXT4_S_IFIFO:  return VFIFO;
	case EXT4_S_IFSOCK: return VSOCK;
	case EXT4_S_IFLNK:  return VLNK;
	default:           return VNON;
	}
}

/*
 * Read one fs block (em_blocksize bytes) at physical block pblk from the
 * underlying device.  The device vnode's block size is set to the fs block
 * size at mount time, so fs block numbers map directly.
 */
int
ext4_blkread(struct ext4mount *emp, uint64_t pblk, buf_t *bpp)
{
	buf_t bp = NULL;
	int error;

	error = buf_meta_bread(emp->em_devvp, (daddr64_t)pblk,
	    (int)emp->em_blocksize, NOCRED, &bp);
	if (error) {
		if (bp)
			buf_brelse(bp);
		*bpp = NULL;
		return error;
	}
	*bpp = bp;
	return 0;
}

int
ext4_read_super(struct ext4mount *emp)
{
	struct ext4_super_block *sb = &emp->em_sb;
	buf_t bp = NULL;
	int error;

	/*
	 * The superblock lives at byte offset 1024.  Read it via a 2048-byte
	 * meta read from device block 0 using a temporary 512-byte view so we
	 * don't depend on the fs blocksize (which we don't know yet).  We read
	 * 4 sectors (2KB) at DEV_BSIZE granularity.
	 */
	error = buf_meta_bread(emp->em_devvp, (daddr64_t)0, 2048, NOCRED, &bp);
	if (error) {
		if (bp) buf_brelse(bp);
		E4LOG("superblock read failed: %d", error);
		return error;
	}

	memcpy(sb, (char *)buf_dataptr(bp) + EXT4_SUPERBLOCK_OFFSET, sizeof(*sb));
	buf_brelse(bp);

	if (le16(sb->s_magic) != EXT4_SUPER_MAGIC) {
		E4LOG("bad magic 0x%x", le16(sb->s_magic));
		return EINVAL;
	}

	emp->em_log_blocksize = le32(sb->s_log_block_size);
	emp->em_blocksize     = 1024u << emp->em_log_blocksize;
	emp->em_inodes_per_group = le32(sb->s_inodes_per_group);
	emp->em_blocks_per_group = le32(sb->s_blocks_per_group);
	emp->em_first_data_block = le32(sb->s_first_data_block);
	emp->em_feature_incompat = le32(sb->s_feature_incompat);

	if (le32(sb->s_rev_level) == 0) {
		emp->em_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
		emp->em_desc_size  = 32;
	} else {
		emp->em_inode_size = le16(sb->s_inode_size);
		if (emp->em_inode_size == 0)
			emp->em_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
		emp->em_desc_size = le16(sb->s_desc_size);
		if ((emp->em_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) == 0 ||
		    emp->em_desc_size < 32)
			emp->em_desc_size = 32;
	}

	emp->em_blocks_count = le32(sb->s_blocks_count_lo);
	if (emp->em_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT)
		emp->em_blocks_count |= ((uint64_t)le32(sb->s_blocks_count_hi)) << 32;
	emp->em_inodes_count = le32(sb->s_inodes_count);

	if (emp->em_blocks_per_group == 0) {
		E4LOG("zero blocks_per_group");
		return EINVAL;
	}
	emp->em_groups_count = (uint32_t)
	    ((emp->em_blocks_count - emp->em_first_data_block +
	     emp->em_blocks_per_group - 1) / emp->em_blocks_per_group);

	E4LOG("mounted: bs=%u ipg=%u bpg=%u isize=%u groups=%u incompat=0x%x blocks=%llu",
	    emp->em_blocksize, emp->em_inodes_per_group, emp->em_blocks_per_group,
	    emp->em_inode_size, emp->em_groups_count, emp->em_feature_incompat,
	    emp->em_blocks_count);
	return 0;
}

/* Read the group descriptor for group `grp` into `gd`. */
static int
ext4_read_group_desc(struct ext4mount *emp, uint32_t grp,
    struct ext4_group_desc *gd)
{
	uint64_t gdt_block;
	uint64_t byteoff;
	uint64_t pblk;
	uint32_t off_in_block;
	buf_t bp = NULL;
	int error;

	/* GDT starts at first_data_block + 1 */
	gdt_block = emp->em_first_data_block + 1;
	byteoff   = (uint64_t)grp * emp->em_desc_size;
	pblk      = gdt_block + (byteoff / emp->em_blocksize);
	off_in_block = (uint32_t)(byteoff % emp->em_blocksize);

	error = ext4_blkread(emp, pblk, &bp);
	if (error)
		return error;

	memset(gd, 0, sizeof(*gd));
	memcpy(gd, (char *)buf_dataptr(bp) + off_in_block,
	    emp->em_desc_size < sizeof(*gd) ? emp->em_desc_size : sizeof(*gd));
	buf_brelse(bp);
	return 0;
}

int
ext4_read_inode(struct ext4mount *emp, ino_t ino, struct ext4_inode *out)
{
	struct ext4_group_desc gd;
	uint32_t grp, index;
	uint64_t itable_block;
	uint64_t byteoff, pblk;
	uint32_t off_in_block;
	buf_t bp = NULL;
	int error;

	if (ino == 0 || ino > emp->em_inodes_count) {
		E4LOG("inode %llu out of range", (uint64_t)ino);
		return EINVAL;
	}

	grp   = (uint32_t)((ino - 1) / emp->em_inodes_per_group);
	index = (uint32_t)((ino - 1) % emp->em_inodes_per_group);

	error = ext4_read_group_desc(emp, grp, &gd);
	if (error)
		return error;

	itable_block = le32(gd.bg_inode_table_lo);
	if (emp->em_desc_size >= 64)
		itable_block |= ((uint64_t)le32(gd.bg_inode_table_hi)) << 32;

	byteoff = (uint64_t)index * emp->em_inode_size;
	pblk    = itable_block + (byteoff / emp->em_blocksize);
	off_in_block = (uint32_t)(byteoff % emp->em_blocksize);

	error = ext4_blkread(emp, pblk, &bp);
	if (error)
		return error;

	memset(out, 0, sizeof(*out));
	memcpy(out, (char *)buf_dataptr(bp) + off_in_block,
	    sizeof(*out) < emp->em_inode_size ? sizeof(*out) : emp->em_inode_size);
	buf_brelse(bp);
	return 0;
}

/* Walk an extent tree node to resolve a logical block. buf holds the node. */
static int
ext4_extent_lookup(struct ext4mount *emp, char *node, uint32_t lblk,
    uint64_t *pblk_out)
{
	struct ext4_extent_header *eh = (struct ext4_extent_header *)node;
	uint16_t entries, depth, i;

	if (le16(eh->eh_magic) != EXT4_EXT_MAGIC) {
		E4LOG("bad extent magic 0x%x", le16(eh->eh_magic));
		return EIO;
	}
	entries = le16(eh->eh_entries);
	depth   = le16(eh->eh_depth);

	if (depth == 0) {
		struct ext4_extent *ex = (struct ext4_extent *)(eh + 1);
		for (i = 0; i < entries; i++) {
			uint32_t first = le32(ex[i].ee_block);
			uint16_t len   = le16(ex[i].ee_len);
			if (len > 32768) len -= 32768;  /* uninitialized extent */
			if (lblk >= first && lblk < first + len) {
				uint64_t start = le32(ex[i].ee_start_lo) |
				    ((uint64_t)le16(ex[i].ee_start_hi) << 32);
				*pblk_out = start + (lblk - first);
				return 0;
			}
		}
		*pblk_out = 0;   /* hole */
		return 0;
	} else {
		struct ext4_extent_idx *ix = (struct ext4_extent_idx *)(eh + 1);
		uint64_t child = 0;
		int found = 0;
		for (i = 0; i < entries; i++) {
			uint32_t first = le32(ix[i].ei_block);
			if (lblk >= first) {
				child = le32(ix[i].ei_leaf_lo) |
				    ((uint64_t)le16(ix[i].ei_leaf_hi) << 32);
				found = 1;
			} else {
				break;
			}
		}
		if (!found) {
			*pblk_out = 0;
			return 0;
		}
		buf_t bp = NULL;
		int error = ext4_blkread(emp, child, &bp);
		if (error)
			return error;
		/* copy node out so we can release the buffer before recursing */
		char *copy = (char *)_MALLOC(emp->em_blocksize, M_TEMP, M_WAITOK);
		if (!copy) { buf_brelse(bp); return ENOMEM; }
		memcpy(copy, (char *)buf_dataptr(bp), emp->em_blocksize);
		buf_brelse(bp);
		error = ext4_extent_lookup(emp, copy, lblk, pblk_out);
		_FREE(copy, M_TEMP);
		return error;
	}
}

/*
 * Map a logical block of an inode to a physical block.  Returns 0 and sets
 * *pblk_out (0 => hole).  Handles both extent-mapped and classic
 * (direct/indirect) inodes.
 */
int
ext4_bmap(struct ext4mount *emp, struct ext4_inode *inode, uint32_t lblk,
    uint64_t *pblk_out)
{
	uint32_t flags = le32(inode->i_flags);

	if (flags & EXT4_EXTENTS_FL) {
		/* extent root lives inline in i_block[] (60 bytes) */
		return ext4_extent_lookup(emp, (char *)inode->i_block, lblk,
		    pblk_out);
	}

	/* classic block map: 12 direct, then single/double/triple indirect */
	uint32_t ptrs = emp->em_blocksize / 4;   /* pointers per indirect block */
	if (lblk < 12) {
		*pblk_out = le32(inode->i_block[lblk]);
		return 0;
	}
	lblk -= 12;
	if (lblk < ptrs) {
		return ext4_indirect_lookup(emp, le32(inode->i_block[12]), lblk,
		    1, pblk_out);
	}
	lblk -= ptrs;
	if (lblk < ptrs * ptrs) {
		return ext4_indirect_lookup(emp, le32(inode->i_block[13]), lblk,
		    2, pblk_out);
	}
	lblk -= ptrs * ptrs;
	return ext4_indirect_lookup(emp, le32(inode->i_block[14]), lblk,
	    3, pblk_out);
}

/* Resolve `lblk` within an N-level indirect block tree rooted at `blk`. */
int
ext4_indirect_lookup(struct ext4mount *emp, uint32_t blk, uint32_t lblk,
    int level, uint64_t *pblk_out)
{
	uint32_t ptrs = emp->em_blocksize / 4;
	uint32_t idx, sub;
	buf_t bp = NULL;
	int error;
	uint32_t next;

	if (blk == 0) { *pblk_out = 0; return 0; }

	if (level == 1) {
		idx = lblk;
	} else if (level == 2) {
		idx = lblk / ptrs;
		sub = lblk % ptrs;
	} else {
		idx = lblk / (ptrs * ptrs);
		sub = lblk % (ptrs * ptrs);
	}

	error = ext4_blkread(emp, blk, &bp);
	if (error)
		return error;
	next = le32(((uint32_t *)buf_dataptr(bp))[idx]);
	buf_brelse(bp);

	if (level == 1) {
		*pblk_out = next;
		return 0;
	}
	return ext4_indirect_lookup(emp, next, sub, level - 1, pblk_out);
}
