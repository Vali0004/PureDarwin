/*
 * ext4_vnops.c - vnode operations for read-only ext4
 */
#include "ext4.h"
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/ubc.h>
#include <sys/vnode_if.h>
#include <sys/dirent.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/unistd.h>
#include <sys/syslimits.h>
#include <string.h>
#include <vm/vm_kern.h>

int (**ext4_vnodeop_p)(void *);

static int ext4_ensure_block(struct ext4node *ep, uint32_t lblk,
    uint64_t *pblk_out);

/*
 * Create (or return) a vnode for inode `ino`.  No inode hash yet: we create a
 * fresh vnode per call and rely on the VFS name cache.  Adequate for a
 * read-only boot volume.
 */
int
ext4_vget(struct ext4mount *emp, ino_t ino, vnode_t dvp, vnode_t *vpp,
    struct componentname *cnp)
{
	struct ext4node *ep;
	struct vnode_fsparam vfsp;
	struct ext4_inode raw;
	enum vtype vtype;
	uint64_t size;
	int error;

	error = ext4_read_inode(emp, ino, &raw);
	if (error)
		return error;

	vtype = ext4_mode_to_vtype(le16(raw.i_mode));
	size  = le32(raw.i_size_lo) | ((uint64_t)le32(raw.i_size_high) << 32);

	ep = (struct ext4node *)_MALLOC(sizeof(*ep), M_TEMP, M_WAITOK | M_ZERO);
	if (ep == NULL)
		return ENOMEM;
	ep->e_mount = emp;
	ep->e_ino   = ino;
	ep->e_raw   = raw;
	ep->e_size  = size;
	ep->e_vtype = vtype;

	memset(&vfsp, 0, sizeof(vfsp));
	vfsp.vnfs_mp        = emp->em_mp;
	vfsp.vnfs_vtype     = vtype;
	vfsp.vnfs_str       = "ext4";
	vfsp.vnfs_dvp       = dvp;
	vfsp.vnfs_fsnode    = ep;
	vfsp.vnfs_vops      = ext4_vnodeop_p;
	vfsp.vnfs_markroot  = (ino == EXT4_ROOT_INO);
	vfsp.vnfs_marksystem = 0;
	vfsp.vnfs_rdev      = 0;
	vfsp.vnfs_filesize  = size;
	vfsp.vnfs_cnp       = cnp;
	vfsp.vnfs_flags     = VNFS_ADDFSREF;
	if (cnp == NULL || !(cnp->cn_flags & MAKEENTRY))
		vfsp.vnfs_flags |= VNFS_NOCACHE;

	error = vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vfsp, vpp);
	if (error) {
		_FREE(ep, M_TEMP);
		return error;
	}
	ep->e_vp = *vpp;
	vnode_settag(*vpp, VT_OTHER);
	return 0;
}

/* --- directory scan --- */

/*
 * Iterate directory `dvp` looking for `name`/`namelen`.  On match returns the
 * child inode number in *ino_out.
 */
static int
ext4_dir_lookup(struct ext4node *dep, const char *name, size_t namelen,
    ino_t *ino_out)
{
	struct ext4mount *emp = dep->e_mount;
	uint64_t off, dsize = dep->e_size;
	uint32_t bs = emp->em_blocksize;
	char *blk;
	int error = ENOENT;

	blk = (char *)_MALLOC(bs, M_TEMP, M_WAITOK);
	if (blk == NULL)
		return ENOMEM;

	for (off = 0; off < dsize; off += bs) {
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;

		if (ext4_bmap(emp, &dep->e_raw, (uint32_t)(off / bs), &pblk))
			continue;
		if (pblk == 0)
			continue;
		if (ext4_blkread(emp, pblk, &bp))
			continue;
		memcpy(blk, (char *)buf_dataptr(bp), bs);
		buf_brelse(bp);

		for (p = 0; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)(blk + p);
			uint16_t reclen = le16(de->rec_len);

			if (reclen < 8 || p + reclen > bs)
				break;
			if (de->inode != 0 && de->name_len == namelen &&
			    memcmp(de->name, name, namelen) == 0) {
				*ino_out = le32(de->inode);
				error = 0;
				goto out;
			}
			p += reclen;
		}
	}
out:
	_FREE(blk, M_TEMP);
	return error;
}

static int
ext4_vnop_lookup(struct vnop_lookup_args *ap)
{
	vnode_t dvp = ap->a_dvp;
	vnode_t *vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct ext4node *dep = VTOE(dvp);
	ino_t ino;
	int error;

	*vpp = NULLVP;

	if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		error = vnode_get(dvp);
		if (error)
			return error;
		*vpp = dvp;
		return 0;
	}

	error = ext4_dir_lookup(dep, cnp->cn_nameptr, cnp->cn_namelen, &ino);
	if (error) {
		if (error == ENOENT && (cnp->cn_flags & ISLASTCN) &&
		    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME))
			return EJUSTRETURN;
		return error;
	}

	return ext4_vget(dep->e_mount, ino, dvp, vpp, cnp);
}

static int
ext4_vnop_getattr(struct vnop_getattr_args *ap)
{
	struct ext4node *ep = VTOE(ap->a_vp);
	struct vnode_attr *vap = ap->a_vap;
	struct ext4_inode *ri = &ep->e_raw;
	struct ext4mount *emp = ep->e_mount;

	VATTR_RETURN(vap, va_rdev, 0);
	VATTR_RETURN(vap, va_nlink, le16(ri->i_links_count));
	VATTR_RETURN(vap, va_data_size, ep->e_size);
	VATTR_RETURN(vap, va_total_size, ep->e_size);
	VATTR_RETURN(vap, va_total_alloc, (uint64_t)le32(ri->i_blocks_lo) * 512);
	VATTR_RETURN(vap, va_iosize, emp->em_blocksize);
	VATTR_RETURN(vap, va_uid, le16(ri->i_uid));
	VATTR_RETURN(vap, va_gid, le16(ri->i_gid));
	VATTR_RETURN(vap, va_mode, le16(ri->i_mode) & 07777);
	VATTR_RETURN(vap, va_fileid, (uint64_t)ep->e_ino);
	VATTR_RETURN(vap, va_type, ep->e_vtype);
	VATTR_RETURN(vap, va_flags, 0);
	VATTR_RETURN(vap, va_gen, le32(ri->i_generation));

	if (VATTR_IS_ACTIVE(vap, va_access_time)) {
		vap->va_access_time.tv_sec  = le32(ri->i_atime);
		vap->va_access_time.tv_nsec = 0;
		VATTR_SET_SUPPORTED(vap, va_access_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_modify_time)) {
		vap->va_modify_time.tv_sec  = le32(ri->i_mtime);
		vap->va_modify_time.tv_nsec = 0;
		VATTR_SET_SUPPORTED(vap, va_modify_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_change_time)) {
		vap->va_change_time.tv_sec  = le32(ri->i_ctime);
		vap->va_change_time.tv_nsec = 0;
		VATTR_SET_SUPPORTED(vap, va_change_time);
	}
	return 0;
}

/* Copy [off, off+len) of the file into a kernel buffer `dst` (len<=bs). */
static int
ext4_read_range(struct ext4node *ep, uint64_t foff, void *dst, size_t len)
{
	struct ext4mount *emp = ep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint32_t lblk = (uint32_t)(foff / bs);
	uint32_t boff = (uint32_t)(foff % bs);
	uint64_t pblk = 0;
	buf_t bp = NULL;
	int error;

	if (boff + len > bs)
		len = bs - boff;

	error = ext4_bmap(emp, &ep->e_raw, lblk, &pblk);
	if (error)
		return error;
	if (pblk == 0) {
		memset(dst, 0, len);   /* hole */
		return 0;
	}
	error = ext4_blkread(emp, pblk, &bp);
	if (error)
		return error;
	memcpy(dst, (char *)buf_dataptr(bp) + boff, len);
	buf_brelse(bp);
	return 0;
}

static int
ext4_vnop_read(struct vnop_read_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ext4node *ep = VTOE(vp);
	uint32_t bs = ep->e_mount->em_blocksize;
	char *buf;
	int error = 0;

	if (vnode_isdir(vp))
		return EISDIR;
	if (uio_offset(uio) < 0)
		return EINVAL;

	buf = (char *)_MALLOC(bs, M_TEMP, M_WAITOK);
	if (buf == NULL)
		return ENOMEM;

	while (uio_resid(uio) > 0) {
		off_t foff = uio_offset(uio);
		size_t want;

		if (foff >= (off_t)ep->e_size)
			break;
		want = bs - (foff % bs);
		if (want > (size_t)uio_resid(uio))
			want = (size_t)uio_resid(uio);
		if (foff + (off_t)want > (off_t)ep->e_size)
			want = (size_t)(ep->e_size - foff);

		error = ext4_read_range(ep, foff, buf, want);
		if (error)
			break;
		error = uiomove(buf, (int)want, uio);
		if (error)
			break;
	}

	_FREE(buf, M_TEMP);
	return error;
}

static uint16_t
ext4_dir_rec_len(uint8_t namelen)
{
	return (uint16_t)((8 + namelen + 3) & ~3);
}

static uint8_t
ext4_vtype_to_ft(enum vtype type)
{
	switch (type) {
	case VREG: return EXT4_FT_REG_FILE;
	case VDIR: return EXT4_FT_DIR;
	case VLNK: return EXT4_FT_SYMLINK;
	case VCHR: return EXT4_FT_CHRDEV;
	case VBLK: return EXT4_FT_BLKDEV;
	case VFIFO: return EXT4_FT_FIFO;
	case VSOCK: return EXT4_FT_SOCK;
	default: return EXT4_FT_UNKNOWN;
	}
}

static int
ext4_dir_add(struct ext4node *dep, const char *name, size_t namelen,
    ino_t ino, enum vtype type)
{
	struct ext4mount *emp = dep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint16_t need;
	uint64_t off;

	if (namelen == 0 || namelen > 255)
		return ENAMETOOLONG;
	need = ext4_dir_rec_len((uint8_t)namelen);

	for (off = 0; off < dep->e_size; off += bs) {
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;
		int error;

		error = ext4_bmap(emp, &dep->e_raw, (uint32_t)(off / bs), &pblk);
		if (error)
			return error;
		if (pblk == 0)
			continue;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;

		for (p = 0; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)((char *)buf_dataptr(bp) + p);
			uint16_t reclen = le16(de->rec_len);
			uint16_t used;

			if (reclen < 8 || p + reclen > bs)
				break;
			used = de->inode == 0 ? 8 : ext4_dir_rec_len(de->name_len);
			if (de->inode == 0 && reclen >= need) {
				used = 0;
			}
			if (reclen >= used + need) {
				struct ext4_dir_entry_2 *nde;
				uint16_t new_reclen = (uint16_t)(reclen - used);

				if (used != 0)
					de->rec_len = le16(used);
				nde = (struct ext4_dir_entry_2 *)((char *)de + used);
				nde->inode = le32((uint32_t)ino);
				nde->rec_len = le16(new_reclen);
				nde->name_len = (uint8_t)namelen;
				nde->file_type = ext4_vtype_to_ft(type);
				memcpy(nde->name, name, namelen);
				return buf_bwrite(bp);
			}
			p += reclen;
		}
		buf_brelse(bp);
	}

	{
		uint64_t pblk = 0;
		buf_t bp = NULL;
		struct ext4_dir_entry_2 *de;
		struct timeval tv;
		int error;

		error = ext4_ensure_block(dep, (uint32_t)(dep->e_size / bs), &pblk);
		if (error)
			return error;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;
		memset((void *)buf_dataptr(bp), 0, bs);
		de = (struct ext4_dir_entry_2 *)buf_dataptr(bp);
		de->inode = le32((uint32_t)ino);
		de->rec_len = le16((uint16_t)bs);
		de->name_len = (uint8_t)namelen;
		de->file_type = ext4_vtype_to_ft(type);
		memcpy(de->name, name, namelen);
		error = buf_bwrite(bp);
		if (error)
			return error;

		dep->e_size += bs;
		dep->e_raw.i_size_lo = le32((uint32_t)dep->e_size);
		dep->e_raw.i_size_high = le32((uint32_t)(dep->e_size >> 32));
		microtime(&tv);
		dep->e_raw.i_ctime = le32((uint32_t)tv.tv_sec);
		dep->e_raw.i_mtime = le32((uint32_t)tv.tv_sec);
		ubc_setsize(dep->e_vp, dep->e_size);
		return ext4_write_inode(emp, dep->e_ino, &dep->e_raw);
	}
}

static int
ext4_dir_remove(struct ext4node *dep, const char *name, size_t namelen,
    ino_t *ino_out, uint8_t *ft_out)
{
	struct ext4mount *emp = dep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t off;

	for (off = 0; off < dep->e_size; off += bs) {
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;
		int error;

		error = ext4_bmap(emp, &dep->e_raw, (uint32_t)(off / bs), &pblk);
		if (error)
			return error;
		if (pblk == 0)
			continue;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;

		for (p = 0; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)((char *)buf_dataptr(bp) + p);
			uint16_t reclen = le16(de->rec_len);

			if (reclen < 8 || p + reclen > bs)
				break;
			if (de->inode != 0 && de->name_len == namelen &&
			    memcmp(de->name, name, namelen) == 0) {
				if (ino_out)
					*ino_out = le32(de->inode);
				if (ft_out)
					*ft_out = de->file_type;
				de->inode = 0;
				return buf_bwrite(bp);
			}
			p += reclen;
		}
		buf_brelse(bp);
	}
	return ENOENT;
}

static int
ext4_dir_replace(struct ext4node *dep, const char *name, size_t namelen,
    ino_t ino, enum vtype type, ino_t *old_ino, uint8_t *old_ft)
{
	struct ext4mount *emp = dep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t off;

	for (off = 0; off < dep->e_size; off += bs) {
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;
		int error;

		error = ext4_bmap(emp, &dep->e_raw, (uint32_t)(off / bs), &pblk);
		if (error)
			return error;
		if (pblk == 0)
			continue;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;

		for (p = 0; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)((char *)buf_dataptr(bp) + p);
			uint16_t reclen = le16(de->rec_len);

			if (reclen < 8 || p + reclen > bs)
				break;
			if (de->inode != 0 && de->name_len == namelen &&
			    memcmp(de->name, name, namelen) == 0) {
				if (old_ino)
					*old_ino = le32(de->inode);
				if (old_ft)
					*old_ft = de->file_type;
				de->inode = le32((uint32_t)ino);
				de->file_type = ext4_vtype_to_ft(type);
				return buf_bwrite(bp);
			}
			p += reclen;
		}
		buf_brelse(bp);
	}
	return ENOENT;
}

static int
ext4_dir_update_dotdot(struct ext4node *dep, ino_t parent)
{
	struct ext4mount *emp = dep->e_mount;
	uint64_t pblk = 0;
	buf_t bp = NULL;
	struct ext4_dir_entry_2 *de;
	uint32_t p;
	int error;

	error = ext4_bmap(emp, &dep->e_raw, 0, &pblk);
	if (error)
		return error;
	if (pblk == 0)
		return EIO;
	error = ext4_blkread(emp, pblk, &bp);
	if (error)
		return error;
	for (p = 0; p < emp->em_blocksize; ) {
		uint16_t reclen;
		de = (struct ext4_dir_entry_2 *)((char *)buf_dataptr(bp) + p);
		reclen = le16(de->rec_len);
		if (reclen < 8 || p + reclen > emp->em_blocksize)
			break;
		if (de->inode != 0 && de->name_len == 2 &&
		    de->name[0] == '.' && de->name[1] == '.') {
			de->inode = le32((uint32_t)parent);
			return buf_bwrite(bp);
		}
		p += reclen;
	}
	buf_brelse(bp);
	return EIO;
}

static int
ext4_dir_is_empty(struct ext4node *dep)
{
	struct ext4mount *emp = dep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t off;

	for (off = 0; off < dep->e_size; off += bs) {
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;
		int error;

		error = ext4_bmap(emp, &dep->e_raw, (uint32_t)(off / bs), &pblk);
		if (error)
			return error;
		if (pblk == 0)
			continue;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;
		for (p = 0; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)((char *)buf_dataptr(bp) + p);
			uint16_t reclen = le16(de->rec_len);

			if (reclen < 8 || p + reclen > bs)
				break;
			if (de->inode != 0 &&
			    !(de->name_len == 1 && de->name[0] == '.') &&
			    !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
				buf_brelse(bp);
				return ENOTEMPTY;
			}
			p += reclen;
		}
		buf_brelse(bp);
	}
	return 0;
}

static void
ext4_touch_inode(struct ext4_inode *ri)
{
	struct timeval tv;
	microtime(&tv);
	ri->i_ctime = le32((uint32_t)tv.tv_sec);
	ri->i_mtime = le32((uint32_t)tv.tv_sec);
}

static int
ext4_drop_inode(struct ext4node *ep)
{
	int error;

	error = ext4_inode_free_extents(ep->e_mount, &ep->e_raw);
	if (error)
		return error;
	ep->e_raw.i_links_count = 0;
	ep->e_raw.i_dtime = ep->e_raw.i_ctime;
	error = ext4_write_inode(ep->e_mount, ep->e_ino, &ep->e_raw);
	if (error)
		return error;
	return ext4_free_inode(ep->e_mount, ep->e_ino, ep->e_vtype);
}

static int
ext4_ensure_block(struct ext4node *ep, uint32_t lblk, uint64_t *pblk_out)
{
	struct ext4mount *emp = ep->e_mount;
	uint64_t pblk = 0;
	int error;

	error = ext4_bmap(emp, &ep->e_raw, lblk, &pblk);
	if (error)
		return error;
	if (pblk != 0) {
		*pblk_out = pblk;
		return 0;
	}

	error = ext4_alloc_block(emp, 0, &pblk);
	if (error)
		return error;
	error = ext4_inode_append_extent(emp, &ep->e_raw, lblk, pblk);
	if (error) {
		(void)ext4_free_block(emp, pblk);
		return error;
	}
	ep->e_raw.i_blocks_lo = le32(le32(ep->e_raw.i_blocks_lo) +
	    (emp->em_blocksize / 512));
	*pblk_out = pblk;
	return 0;
}

static int
ext4_vnop_write(struct vnop_write_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ext4node *ep = VTOE(vp);
	struct ext4mount *emp = ep->e_mount;
	uint32_t bs = emp->em_blocksize;
	int error = 0;
	bool dirty = false;

	if (vnode_isdir(vp))
		return EISDIR;
	if (ap->a_ioflag & IO_APPEND)
		uio_setoffset(uio, (off_t)ep->e_size);
	if (uio_offset(uio) < 0)
		return EINVAL;

	while (uio_resid(uio) > 0) {
		off_t foff = uio_offset(uio);
		uint32_t lblk = (uint32_t)(foff / bs);
		uint32_t boff = (uint32_t)(foff % bs);
		size_t want = bs - boff;
		uint64_t pblk = 0;
		buf_t bp = NULL;

		if (want > (size_t)uio_resid(uio))
			want = (size_t)uio_resid(uio);

		if (foff > (off_t)ep->e_size) {
			error = EFBIG;   /* no sparse file creation yet */
			break;
		}
		error = ext4_ensure_block(ep, lblk, &pblk);
		if (error)
			break;
		error = ext4_blkread(emp, pblk, &bp);   /* read-modify-write */
		if (error)
			break;
		error = uiomove((char *)buf_dataptr(bp) + boff, (int)want, uio);
		if (error) {
			buf_brelse(bp);
			break;
		}
		error = buf_bwrite(bp);   /* synchronous; releases bp */
		if (error)
			break;
		if (uio_offset(uio) > (off_t)ep->e_size)
			ep->e_size = (uint64_t)uio_offset(uio);
		dirty = true;
	}

	if (dirty) {
		struct timeval tv;
		microtime(&tv);
		ep->e_raw.i_size_lo = le32((uint32_t)ep->e_size);
		ep->e_raw.i_size_high = le32((uint32_t)(ep->e_size >> 32));
		ep->e_raw.i_ctime = le32((uint32_t)tv.tv_sec);
		ep->e_raw.i_mtime = le32((uint32_t)tv.tv_sec);
		ubc_setsize(vp, ep->e_size);
		(void)ext4_write_inode(emp, ep->e_ino, &ep->e_raw);
	}
	return error;
}

static int
ext4_zero_range(struct ext4node *ep, uint64_t start, uint64_t end)
{
	struct ext4mount *emp = ep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t off;
	int error;

	for (off = start; off < end; ) {
		uint32_t lblk = (uint32_t)(off / bs);
		uint32_t boff = (uint32_t)(off % bs);
		size_t chunk = bs - boff;
		uint64_t pblk = 0;
		buf_t bp = NULL;

		if (chunk > end - off)
			chunk = (size_t)(end - off);

		error = ext4_ensure_block(ep, lblk, &pblk);
		if (error)
			return error;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;
		memset((char *)buf_dataptr(bp) + boff, 0, chunk);
		error = buf_bwrite(bp);
		if (error)
			return error;
		off += chunk;
	}
	return 0;
}

static int
ext4_resize_file(struct ext4node *ep, uint64_t new_size)
{
	struct ext4mount *emp = ep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t old_size = ep->e_size;
	uint64_t old_blocks = (ep->e_size + bs - 1) / bs;
	uint64_t new_blocks = (new_size + bs - 1) / bs;
	uint64_t i;
	int error;

	if (ep->e_vtype != VREG)
		return EISDIR;
	if (new_blocks < old_blocks) {
		if ((new_size % bs) != 0 && new_blocks != 0) {
			uint64_t pblk = 0;
			buf_t bp = NULL;
			uint32_t tail = (uint32_t)(new_size % bs);

			error = ext4_bmap(emp, &ep->e_raw, (uint32_t)(new_blocks - 1), &pblk);
			if (error)
				return error;
			if (pblk != 0) {
				error = ext4_blkread(emp, pblk, &bp);
				if (error)
					return error;
				memset((char *)buf_dataptr(bp) + tail, 0, bs - tail);
				error = buf_bwrite(bp);
				if (error)
					return error;
			}
		}
		error = ext4_inode_truncate_extents(emp, &ep->e_raw, new_blocks);
		if (error)
			return error;
	} else {
		for (i = old_blocks; i < new_blocks; i++) {
			uint64_t pblk;
			error = ext4_ensure_block(ep, (uint32_t)i, &pblk);
			if (error)
				return error;
		}
		error = ext4_zero_range(ep, old_size, new_size);
		if (error)
			return error;
	}
	ep->e_size = new_size;
	ep->e_raw.i_size_lo = le32((uint32_t)new_size);
	ep->e_raw.i_size_high = le32((uint32_t)(new_size >> 32));
	ext4_touch_inode(&ep->e_raw);
	ubc_setsize(ep->e_vp, ep->e_size);
	return ext4_write_inode(emp, ep->e_ino, &ep->e_raw);
}

static int
ext4_vnop_setattr(struct vnop_setattr_args *ap)
{
	struct ext4node *ep = VTOE(ap->a_vp);
	struct vnode_attr *vap = ap->a_vap;
	int error = 0;

	if (VATTR_IS_ACTIVE(vap, va_data_size)) {
		error = ext4_resize_file(ep, vap->va_data_size);
		if (error)
			return error;
		VATTR_SET_SUPPORTED(vap, va_data_size);
	}
	if (VATTR_IS_ACTIVE(vap, va_mode)) {
		ep->e_raw.i_mode = le16((uint16_t)((le16(ep->e_raw.i_mode) & EXT4_S_IFMT) |
		    (vap->va_mode & 07777)));
		VATTR_SET_SUPPORTED(vap, va_mode);
	}
	if (VATTR_IS_ACTIVE(vap, va_uid)) {
		ep->e_raw.i_uid = le16((uint16_t)vap->va_uid);
		VATTR_SET_SUPPORTED(vap, va_uid);
	}
	if (VATTR_IS_ACTIVE(vap, va_gid)) {
		ep->e_raw.i_gid = le16((uint16_t)vap->va_gid);
		VATTR_SET_SUPPORTED(vap, va_gid);
	}
	if (VATTR_IS_ACTIVE(vap, va_access_time)) {
		ep->e_raw.i_atime = le32((uint32_t)vap->va_access_time.tv_sec);
		VATTR_SET_SUPPORTED(vap, va_access_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_modify_time)) {
		ep->e_raw.i_mtime = le32((uint32_t)vap->va_modify_time.tv_sec);
		VATTR_SET_SUPPORTED(vap, va_modify_time);
	}
	ext4_touch_inode(&ep->e_raw);
	return ext4_write_inode(ep->e_mount, ep->e_ino, &ep->e_raw);
}

static int
ext4_vnop_create(struct vnop_create_args *ap)
{
	vnode_t dvp = ap->a_dvp;
	vnode_t *vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode_attr *vap = ap->a_vap;
	struct ext4node *dep = VTOE(dvp);
	struct ext4mount *emp = dep->e_mount;
	struct ext4_inode raw;
	struct timeval tv;
	ino_t ino;
	mode_t mode = 0644;
	uid_t uid = 0;
	gid_t gid = 0;
	int error;

	*vpp = NULLVP;
	if (!vnode_isdir(dvp))
		return ENOTDIR;
	if (VATTR_IS_ACTIVE(vap, va_type) && vap->va_type != VREG)
		return ENOTSUP;
	if (cnp->cn_namelen == 0 || cnp->cn_namelen > 255)
		return ENAMETOOLONG;

	error = ext4_dir_lookup(dep, cnp->cn_nameptr, cnp->cn_namelen, &ino);
	if (error == 0)
		return EEXIST;
	if (error != ENOENT)
		return error;

	error = ext4_alloc_inode(emp, VREG, &ino);
	if (error)
		return error;

	if (VATTR_IS_ACTIVE(vap, va_mode))
		mode = vap->va_mode & 07777;
	if (VATTR_IS_ACTIVE(vap, va_uid))
		uid = vap->va_uid;
	if (VATTR_IS_ACTIVE(vap, va_gid))
		gid = vap->va_gid;

	memset(&raw, 0, sizeof(raw));
	microtime(&tv);
	raw.i_mode = le16((uint16_t)(EXT4_S_IFREG | mode));
	raw.i_uid = le16((uint16_t)uid);
	raw.i_gid = le16((uint16_t)gid);
	raw.i_atime = le32((uint32_t)tv.tv_sec);
	raw.i_ctime = le32((uint32_t)tv.tv_sec);
	raw.i_mtime = le32((uint32_t)tv.tv_sec);
	raw.i_links_count = le16(1);
	raw.i_flags = le32(EXT4_EXTENTS_FL);
	raw.i_extra_isize = le16(emp->em_inode_size > EXT4_GOOD_OLD_INODE_SIZE ?
	    (uint16_t)(emp->em_inode_size - EXT4_GOOD_OLD_INODE_SIZE) : 0);
	(void)ext4_inode_append_extent(emp, &raw, 0, 0);
	((struct ext4_extent_header *)raw.i_block)->eh_entries = 0;

	error = ext4_write_inode(emp, ino, &raw);
	if (error) {
		(void)ext4_free_inode(emp, ino, VREG);
		return error;
	}
	error = ext4_dir_add(dep, cnp->cn_nameptr, cnp->cn_namelen, ino, VREG);
	if (error) {
		raw.i_links_count = 0;
		(void)ext4_write_inode(emp, ino, &raw);
		(void)ext4_free_inode(emp, ino, VREG);
		return error;
	}
	dep->e_raw.i_ctime = le32((uint32_t)tv.tv_sec);
	dep->e_raw.i_mtime = le32((uint32_t)tv.tv_sec);
	(void)ext4_write_inode(emp, dep->e_ino, &dep->e_raw);

	VATTR_SET_SUPPORTED(vap, va_type);
	VATTR_SET_SUPPORTED(vap, va_mode);
	VATTR_SET_SUPPORTED(vap, va_uid);
	VATTR_SET_SUPPORTED(vap, va_gid);

	return ext4_vget(emp, ino, dvp, vpp, cnp);
}

static int
ext4_seed_dir_block(struct ext4mount *emp, uint64_t pblk, ino_t self, ino_t parent)
{
	buf_t bp = NULL;
	struct ext4_dir_entry_2 *de;
	char *data;
	int error;

	error = ext4_blkread(emp, pblk, &bp);
	if (error)
		return error;
	data = (char *)buf_dataptr(bp);
	memset(data, 0, emp->em_blocksize);

	de = (struct ext4_dir_entry_2 *)data;
	de->inode = le32((uint32_t)self);
	de->rec_len = le16(ext4_dir_rec_len(1));
	de->name_len = 1;
	de->file_type = EXT4_FT_DIR;
	de->name[0] = '.';

	de = (struct ext4_dir_entry_2 *)(data + ext4_dir_rec_len(1));
	de->inode = le32((uint32_t)parent);
	de->rec_len = le16((uint16_t)(emp->em_blocksize - ext4_dir_rec_len(1)));
	de->name_len = 2;
	de->file_type = EXT4_FT_DIR;
	de->name[0] = '.';
	de->name[1] = '.';

	return buf_bwrite(bp);
}

static int
ext4_vnop_mkdir(struct vnop_mkdir_args *ap)
{
	vnode_t dvp = ap->a_dvp;
	vnode_t *vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode_attr *vap = ap->a_vap;
	struct ext4node *dep = VTOE(dvp);
	struct ext4mount *emp = dep->e_mount;
	struct ext4_inode raw;
	struct timeval tv;
	ino_t ino;
	uint64_t pblk;
	mode_t mode = 0755;
	uid_t uid = 0;
	gid_t gid = 0;
	int error;

	*vpp = NULLVP;
	if (!vnode_isdir(dvp))
		return ENOTDIR;
	if (cnp->cn_namelen == 0 || cnp->cn_namelen > 255)
		return ENAMETOOLONG;

	error = ext4_dir_lookup(dep, cnp->cn_nameptr, cnp->cn_namelen, &ino);
	if (error == 0)
		return EEXIST;
	if (error != ENOENT)
		return error;

	error = ext4_alloc_inode(emp, VDIR, &ino);
	if (error)
		return error;
	error = ext4_alloc_block(emp, 0, &pblk);
	if (error) {
		(void)ext4_free_inode(emp, ino, VDIR);
		return error;
	}

	if (VATTR_IS_ACTIVE(vap, va_mode))
		mode = vap->va_mode & 07777;
	if (VATTR_IS_ACTIVE(vap, va_uid))
		uid = vap->va_uid;
	if (VATTR_IS_ACTIVE(vap, va_gid))
		gid = vap->va_gid;

	memset(&raw, 0, sizeof(raw));
	microtime(&tv);
	raw.i_mode = le16((uint16_t)(EXT4_S_IFDIR | mode));
	raw.i_uid = le16((uint16_t)uid);
	raw.i_gid = le16((uint16_t)gid);
	raw.i_size_lo = le32(emp->em_blocksize);
	raw.i_atime = le32((uint32_t)tv.tv_sec);
	raw.i_ctime = le32((uint32_t)tv.tv_sec);
	raw.i_mtime = le32((uint32_t)tv.tv_sec);
	raw.i_links_count = le16(2);
	raw.i_blocks_lo = le32(emp->em_blocksize / 512);
	raw.i_flags = le32(EXT4_EXTENTS_FL);
	raw.i_extra_isize = le16(emp->em_inode_size > EXT4_GOOD_OLD_INODE_SIZE ?
	    (uint16_t)(emp->em_inode_size - EXT4_GOOD_OLD_INODE_SIZE) : 0);
	error = ext4_inode_append_extent(emp, &raw, 0, pblk);
	if (error) {
		(void)ext4_free_block(emp, pblk);
		(void)ext4_free_inode(emp, ino, VDIR);
		return error;
	}
	error = ext4_seed_dir_block(emp, pblk, ino, dep->e_ino);
	if (error) {
		(void)ext4_inode_free_extents(emp, &raw);
		(void)ext4_free_inode(emp, ino, VDIR);
		return error;
	}
	error = ext4_write_inode(emp, ino, &raw);
	if (error) {
		(void)ext4_inode_free_extents(emp, &raw);
		(void)ext4_free_inode(emp, ino, VDIR);
		return error;
	}
	error = ext4_dir_add(dep, cnp->cn_nameptr, cnp->cn_namelen, ino, VDIR);
	if (error) {
		(void)ext4_inode_free_extents(emp, &raw);
		(void)ext4_free_inode(emp, ino, VDIR);
		return error;
	}

	dep->e_raw.i_links_count = le16((uint16_t)(le16(dep->e_raw.i_links_count) + 1));
	dep->e_raw.i_ctime = le32((uint32_t)tv.tv_sec);
	dep->e_raw.i_mtime = le32((uint32_t)tv.tv_sec);
	(void)ext4_write_inode(emp, dep->e_ino, &dep->e_raw);

	VATTR_SET_SUPPORTED(vap, va_type);
	VATTR_SET_SUPPORTED(vap, va_mode);
	VATTR_SET_SUPPORTED(vap, va_uid);
	VATTR_SET_SUPPORTED(vap, va_gid);

	return ext4_vget(emp, ino, dvp, vpp, cnp);
}

static int
ext4_vnop_remove(struct vnop_remove_args *ap)
{
	struct ext4node *dep = VTOE(ap->a_dvp);
	struct ext4node *ep = VTOE(ap->a_vp);
	ino_t removed;
	int error;

	if (vnode_isdir(ap->a_vp))
		return EISDIR;
	error = ext4_dir_remove(dep, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
	    &removed, NULL);
	if (error)
		return error;
	if (removed != ep->e_ino)
		return EIO;
	ext4_touch_inode(&dep->e_raw);
	(void)ext4_write_inode(dep->e_mount, dep->e_ino, &dep->e_raw);
	return ext4_drop_inode(ep);
}

static int
ext4_vnop_rmdir(struct vnop_rmdir_args *ap)
{
	struct ext4node *dep = VTOE(ap->a_dvp);
	struct ext4node *ep = VTOE(ap->a_vp);
	ino_t removed;
	int error;

	if (!vnode_isdir(ap->a_vp))
		return ENOTDIR;
	if (ap->a_cnp->cn_namelen == 1 && ap->a_cnp->cn_nameptr[0] == '.')
		return EINVAL;
	error = ext4_dir_is_empty(ep);
	if (error)
		return error;
	error = ext4_dir_remove(dep, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
	    &removed, NULL);
	if (error)
		return error;
	if (removed != ep->e_ino)
		return EIO;
	if (le16(dep->e_raw.i_links_count) > 0)
		dep->e_raw.i_links_count =
		    le16((uint16_t)(le16(dep->e_raw.i_links_count) - 1));
	ext4_touch_inode(&dep->e_raw);
	(void)ext4_write_inode(dep->e_mount, dep->e_ino, &dep->e_raw);
	return ext4_drop_inode(ep);
}

static int
ext4_vnop_rename(struct vnop_rename_args *ap)
{
	struct ext4node *fdep = VTOE(ap->a_fdvp);
	struct ext4node *fep = VTOE(ap->a_fvp);
	struct ext4node *tdep = VTOE(ap->a_tdvp);
	struct ext4node *tep = ap->a_tvp ? VTOE(ap->a_tvp) : NULL;
	bool moving_dir = vnode_isdir(ap->a_fvp);
	bool cross_dir = ap->a_fdvp != ap->a_tdvp;
	int error;

	if (ap->a_fvp == ap->a_tvp)
		return 0;
	if (moving_dir) {
		if (tep && !vnode_isdir(ap->a_tvp))
			return ENOTDIR;
		if (tep) {
			error = ext4_dir_is_empty(tep);
			if (error)
				return error;
		}
	} else if (tep && vnode_isdir(ap->a_tvp)) {
		return EISDIR;
	}

	if (tep) {
		ino_t old_ino = 0;
		uint8_t old_ft = EXT4_FT_UNKNOWN;

		error = ext4_dir_replace(tdep, ap->a_tcnp->cn_nameptr,
		    ap->a_tcnp->cn_namelen, fep->e_ino, fep->e_vtype,
		    &old_ino, &old_ft);
		if (error)
			return error;
		error = ext4_dir_remove(fdep, ap->a_fcnp->cn_nameptr,
		    ap->a_fcnp->cn_namelen, NULL, NULL);
		if (error) {
			(void)ext4_dir_replace(tdep, ap->a_tcnp->cn_nameptr,
			    ap->a_tcnp->cn_namelen, old_ino,
			    ext4_ft_to_vtype(old_ft), NULL, NULL);
			return error;
		}
		if (vnode_isdir(ap->a_tvp) && le16(tdep->e_raw.i_links_count) > 0)
			tdep->e_raw.i_links_count =
			    le16((uint16_t)(le16(tdep->e_raw.i_links_count) - 1));
		(void)ext4_drop_inode(tep);
	} else {
		error = ext4_dir_add(tdep, ap->a_tcnp->cn_nameptr,
		    ap->a_tcnp->cn_namelen, fep->e_ino, fep->e_vtype);
		if (error)
			return error;
		error = ext4_dir_remove(fdep, ap->a_fcnp->cn_nameptr,
		    ap->a_fcnp->cn_namelen, NULL, NULL);
		if (error) {
			(void)ext4_dir_remove(tdep, ap->a_tcnp->cn_nameptr,
			    ap->a_tcnp->cn_namelen, NULL, NULL);
			return error;
		}
	}
	if (moving_dir && cross_dir) {
		(void)ext4_dir_update_dotdot(fep, tdep->e_ino);
		if (le16(fdep->e_raw.i_links_count) > 0)
			fdep->e_raw.i_links_count =
			    le16((uint16_t)(le16(fdep->e_raw.i_links_count) - 1));
		tdep->e_raw.i_links_count =
		    le16((uint16_t)(le16(tdep->e_raw.i_links_count) + 1));
	}
	ext4_touch_inode(&fdep->e_raw);
	ext4_touch_inode(&tdep->e_raw);
	(void)ext4_write_inode(fdep->e_mount, fdep->e_ino, &fdep->e_raw);
	if (tdep != fdep)
		(void)ext4_write_inode(tdep->e_mount, tdep->e_ino, &tdep->e_raw);
	return 0;
}

static int
ext4_vnop_symlink(struct vnop_symlink_args *ap)
{
	vnode_t dvp = ap->a_dvp;
	vnode_t *vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode_attr *vap = ap->a_vap;
	struct ext4node *dep = VTOE(dvp);
	struct ext4mount *emp = dep->e_mount;
	struct ext4_inode raw;
	struct timeval tv;
	ino_t ino;
	size_t len = strlen(ap->a_target);
	mode_t mode = 0777;
	uid_t uid = 0;
	gid_t gid = 0;
	int error;

	*vpp = NULLVP;
	if (!vnode_isdir(dvp))
		return ENOTDIR;
	if (cnp->cn_namelen == 0 || cnp->cn_namelen > 255)
		return ENAMETOOLONG;
	if (len > emp->em_blocksize)
		return ENAMETOOLONG;

	error = ext4_dir_lookup(dep, cnp->cn_nameptr, cnp->cn_namelen, &ino);
	if (error == 0)
		return EEXIST;
	if (error != ENOENT)
		return error;

	error = ext4_alloc_inode(emp, VLNK, &ino);
	if (error)
		return error;

	if (VATTR_IS_ACTIVE(vap, va_mode))
		mode = vap->va_mode & 07777;
	if (VATTR_IS_ACTIVE(vap, va_uid))
		uid = vap->va_uid;
	if (VATTR_IS_ACTIVE(vap, va_gid))
		gid = vap->va_gid;

	memset(&raw, 0, sizeof(raw));
	microtime(&tv);
	raw.i_mode = le16((uint16_t)(EXT4_S_IFLNK | mode));
	raw.i_uid = le16((uint16_t)uid);
	raw.i_gid = le16((uint16_t)gid);
	raw.i_size_lo = le32((uint32_t)len);
	raw.i_size_high = le32((uint32_t)((uint64_t)len >> 32));
	raw.i_atime = le32((uint32_t)tv.tv_sec);
	raw.i_ctime = le32((uint32_t)tv.tv_sec);
	raw.i_mtime = le32((uint32_t)tv.tv_sec);
	raw.i_links_count = le16(1);
	raw.i_extra_isize = le16(emp->em_inode_size > EXT4_GOOD_OLD_INODE_SIZE ?
	    (uint16_t)(emp->em_inode_size - EXT4_GOOD_OLD_INODE_SIZE) : 0);

	if (len < sizeof(raw.i_block)) {
		memcpy(raw.i_block, ap->a_target, len);
	} else {
		uint64_t pblk = 0;
		buf_t bp = NULL;

		raw.i_flags = le32(EXT4_EXTENTS_FL);
		error = ext4_alloc_block(emp, 0, &pblk);
		if (error)
			goto fail_inode;
		error = ext4_inode_append_extent(emp, &raw, 0, pblk);
		if (error) {
			(void)ext4_free_block(emp, pblk);
			goto fail_inode;
		}
		raw.i_blocks_lo = le32(emp->em_blocksize / 512);
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			goto fail_extents;
		memset((void *)buf_dataptr(bp), 0, emp->em_blocksize);
		memcpy((void *)buf_dataptr(bp), ap->a_target, len);
		error = buf_bwrite(bp);
		if (error)
			goto fail_extents;
	}

	error = ext4_write_inode(emp, ino, &raw);
	if (error)
		goto fail_extents;
	error = ext4_dir_add(dep, cnp->cn_nameptr, cnp->cn_namelen, ino, VLNK);
	if (error)
		goto fail_extents;
	ext4_touch_inode(&dep->e_raw);
	(void)ext4_write_inode(emp, dep->e_ino, &dep->e_raw);
	return ext4_vget(emp, ino, dvp, vpp, cnp);

fail_extents:
	(void)ext4_inode_free_extents(emp, &raw);
fail_inode:
	(void)ext4_free_inode(emp, ino, VLNK);
	return error;
}

static int
ext4_vnop_link(struct vnop_link_args *ap)
{
	struct ext4node *ep = VTOE(ap->a_vp);
	struct ext4node *tdp = VTOE(ap->a_tdvp);
	ino_t existing;
	uint16_t links;
	int error;

	if (vnode_isdir(ap->a_vp))
		return EPERM;
	if (!vnode_isdir(ap->a_tdvp))
		return ENOTDIR;
	if (ap->a_cnp->cn_namelen == 0 || ap->a_cnp->cn_namelen > 255)
		return ENAMETOOLONG;

	error = ext4_dir_lookup(tdp, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
	    &existing);
	if (error == 0)
		return EEXIST;
	if (error != ENOENT)
		return error;

	links = le16(ep->e_raw.i_links_count);
	if (links == UINT16_MAX)
		return EMLINK;

	error = ext4_dir_add(tdp, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
	    ep->e_ino, ep->e_vtype);
	if (error)
		return error;

	ep->e_raw.i_links_count = le16((uint16_t)(links + 1));
	ext4_touch_inode(&ep->e_raw);
	error = ext4_write_inode(ep->e_mount, ep->e_ino, &ep->e_raw);
	if (error) {
		(void)ext4_dir_remove(tdp, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen,
		    NULL, NULL);
		ep->e_raw.i_links_count = le16(links);
		return error;
	}
	ext4_touch_inode(&tdp->e_raw);
	(void)ext4_write_inode(tdp->e_mount, tdp->e_ino, &tdp->e_raw);
	return 0;
}

static int
ext4_vnop_readdir(struct vnop_readdir_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ext4node *dep = VTOE(vp);
	struct ext4mount *emp = dep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint64_t off = (uint64_t)uio_offset(uio);
	char *blk;
	int error = 0;
	int numdir = 0;

	if (ap->a_flags & VNODE_READDIR_EXTENDED)
		return EINVAL;   /* only classic dirent supported */
	if (!vnode_isdir(vp))
		return ENOTDIR;

	blk = (char *)_MALLOC(bs, M_TEMP, M_WAITOK);
	if (blk == NULL)
		return ENOMEM;

	while (off < dep->e_size && uio_resid(uio) > 0) {
		uint64_t blkoff = (off / bs) * bs;
		uint32_t within = (uint32_t)(off - blkoff);
		uint64_t pblk = 0;
		buf_t bp = NULL;
		uint32_t p;

		if (ext4_bmap(emp, &dep->e_raw, (uint32_t)(blkoff / bs), &pblk) ||
		    pblk == 0) {
			off = blkoff + bs;
			continue;
		}
		if (ext4_blkread(emp, pblk, &bp)) {
			off = blkoff + bs;
			continue;
		}
		memcpy(blk, (char *)buf_dataptr(bp), bs);
		buf_brelse(bp);

		for (p = within; p < bs; ) {
			struct ext4_dir_entry_2 *de =
			    (struct ext4_dir_entry_2 *)(blk + p);
			uint16_t reclen = le16(de->rec_len);
			struct dirent dent;

			if (reclen < 8 || p + reclen > bs)
				break;
			if (de->inode != 0) {
				uint8_t nl = de->name_len;
				if (nl > NAME_MAX) nl = NAME_MAX;
				memset(&dent, 0, sizeof(dent));
				dent.d_ino    = le32(de->inode);
				dent.d_type   = de->file_type;
				dent.d_namlen = nl;
				memcpy(dent.d_name, de->name, nl);
				dent.d_name[nl] = '\0';
				dent.d_reclen = (uint16_t)
				    (offsetof(struct dirent, d_name) + nl + 1 + 3) & ~3;

				if ((user_size_t)dent.d_reclen > uio_resid(uio))
					goto done;
				error = uiomove((caddr_t)&dent, dent.d_reclen, uio);
				if (error)
					goto done;
				numdir++;
			}
			p += reclen;
			off = blkoff + p;
		}
		off = blkoff + bs;
	}
done:
	_FREE(blk, M_TEMP);
	uio_setoffset(uio, off);
	if (ap->a_eofflag)
		*ap->a_eofflag = (off >= dep->e_size);
	if (ap->a_numdirent)
		*ap->a_numdirent = numdir;
	return error;
}

static int
ext4_vnop_readlink(struct vnop_readlink_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ext4node *ep = VTOE(vp);
	struct ext4mount *emp = ep->e_mount;
	uint64_t size = ep->e_size;
	int error;

	if (!vnode_islnk(vp))
		return EINVAL;

	/* fast symlink: target stored inline in i_block[] when size < 60 */
	if (size < 60 && le32(ep->e_raw.i_blocks_lo) == 0) {
		return uiomove((caddr_t)ep->e_raw.i_block, (int)size, uio);
	}

	/* slow symlink: target in the first data block */
	{
		uint64_t pblk = 0;
		buf_t bp = NULL;
		error = ext4_bmap(emp, &ep->e_raw, 0, &pblk);
		if (error)
			return error;
		if (pblk == 0)
			return EIO;
		error = ext4_blkread(emp, pblk, &bp);
		if (error)
			return error;
		error = uiomove((caddr_t)buf_dataptr(bp),
		    (int)(size > emp->em_blocksize ? emp->em_blocksize : size), uio);
		buf_brelse(bp);
		return error;
	}
}

static int
ext4_vnop_pagein(struct vnop_pagein_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct ext4node *ep = VTOE(vp);
	upl_t pl = ap->a_pl;
	vm_offset_t ioaddr = 0;
	off_t f_offset = ap->a_f_offset;
	size_t size = ap->a_size;
	kern_return_t kr;
	int error = 0;
	size_t done;

	kr = ubc_upl_map(pl, &ioaddr);
	if (kr != KERN_SUCCESS)
		return EIO;
	ioaddr += ap->a_pl_offset;

	for (done = 0; done < size; ) {
		char *dst = (char *)ioaddr + done;
		off_t foff = f_offset + done;
		size_t chunk = ep->e_mount->em_blocksize;

		chunk -= (foff % ep->e_mount->em_blocksize);
		if (chunk > size - done)
			chunk = size - done;

		if (foff >= (off_t)ep->e_size) {
			memset(dst, 0, size - done);   /* beyond EOF */
			break;
		}
		if (foff + (off_t)chunk > (off_t)ep->e_size)
			chunk = (size_t)(ep->e_size - foff);

		error = ext4_read_range(ep, foff, dst, chunk);
		if (error)
			break;
		done += chunk;
	}

	ubc_upl_unmap(pl);

	if (!(ap->a_flags & UPL_NOCOMMIT)) {
		if (error)
			ubc_upl_abort_range(pl, ap->a_pl_offset, size,
			    UPL_ABORT_ERROR | UPL_ABORT_FREE_ON_EMPTY);
		else
			ubc_upl_commit_range(pl, ap->a_pl_offset, size,
			    UPL_COMMIT_FREE_ON_EMPTY);
	}
	return error;
}

static int
ext4_vnop_pageout(struct vnop_pageout_args *ap)
{
	struct ext4node *ep = VTOE(ap->a_vp);
	off_t filesize = (off_t)ep->e_size;

	return cluster_pageout(ap->a_vp, ap->a_pl, ap->a_pl_offset,
	    ap->a_f_offset, (int)ap->a_size, filesize, ap->a_flags);
}

static int
ext4_vnop_blockmap(struct vnop_blockmap_args *ap)
{
	struct ext4node *ep = VTOE(ap->a_vp);
	struct ext4mount *emp = ep->e_mount;
	uint32_t bs = emp->em_blocksize;
	uint32_t lblk;
	uint32_t boff;
	uint64_t pblk = 0;
	uint64_t next_pblk;
	size_t run;
	int error;

	if (vnode_isdir(ap->a_vp))
		return ENOTSUP;
	if (ap->a_bpn == NULL)
		return 0;
	if (ap->a_foffset < 0)
		return EINVAL;

	lblk = (uint32_t)(ap->a_foffset / bs);
	boff = (uint32_t)(ap->a_foffset % bs);
	error = ext4_bmap(emp, &ep->e_raw, lblk, &pblk);
	if (error)
		return error;
	if (pblk == 0) {
		if (ap->a_flags & VNODE_WRITE)
			return EINVAL;
		*ap->a_bpn = (daddr64_t)-1;
		if (ap->a_run)
			*ap->a_run = bs - boff;
		return 0;
	}

	run = bs - boff;
	while (run < ap->a_size) {
		uint32_t next_lblk = lblk + 1 + (uint32_t)((run - (bs - boff)) / bs);
		uint64_t expect = pblk + 1 + ((run - (bs - boff)) / bs);

		error = ext4_bmap(emp, &ep->e_raw, next_lblk, &next_pblk);
		if (error || next_pblk != expect)
			break;
		run += bs;
	}
	*ap->a_bpn = (daddr64_t)pblk;
	if (ap->a_run)
		*ap->a_run = run;
	return 0;
}

static int
ext4_vnop_reclaim(struct vnop_reclaim_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct ext4node *ep = VTOE(vp);

	vnode_removefsref(vp);
	vnode_clearfsnode(vp);
	if (ep)
		_FREE(ep, M_TEMP);
	return 0;
}

static int
ext4_vnop_open(__unused struct vnop_open_args *ap)
{
	return 0;
}

static int
ext4_vnop_close(__unused struct vnop_close_args *ap)
{
	return 0;
}

static int
ext4_vnop_inactive(__unused struct vnop_inactive_args *ap)
{
	return 0;
}

/*
 * Read-only fs: nothing to flush. But this MUST be implemented (not left to
 * vnop_default, which returns ENOTSUP): mount_common() calls VNOP_FSYNC on a
 * mountpoint's covered vnode, so without this every submount onto an ext4
 * root - notably devfs at /dev - fails with ENOTSUP (error 45), leaving
 * userland with no /dev/console and launchd unable to start.
 */
static int
ext4_vnop_fsync(__unused struct vnop_fsync_args *ap)
{
	return 0;
}

static int
ext4_vnop_pathconf(struct vnop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:      *ap->a_retval = 65000; return 0;
	case _PC_NAME_MAX:      *ap->a_retval = 255;   return 0;
	case _PC_PATH_MAX:      *ap->a_retval = PATH_MAX; return 0;
	case _PC_CHOWN_RESTRICTED: *ap->a_retval = 1;  return 0;
	case _PC_NO_TRUNC:      *ap->a_retval = 1;      return 0;
	case _PC_CASE_SENSITIVE: *ap->a_retval = 1;     return 0;
	case _PC_CASE_PRESERVING: *ap->a_retval = 1;    return 0;
	default:                return EINVAL;
	}
}

static int
ext4_vnop_default(__unused struct vnop_generic_args *ap)
{
	return ENOTSUP;
}

#define VOPFUNC int (*)(void *)

static const struct vnodeopv_entry_desc ext4_vnodeop_entries[] = {
	{ &vnop_default_desc,  (VOPFUNC)ext4_vnop_default },
	{ &vnop_lookup_desc,   (VOPFUNC)ext4_vnop_lookup },
	{ &vnop_create_desc,   (VOPFUNC)ext4_vnop_create },
	{ &vnop_mkdir_desc,    (VOPFUNC)ext4_vnop_mkdir },
	{ &vnop_open_desc,     (VOPFUNC)ext4_vnop_open },
	{ &vnop_close_desc,    (VOPFUNC)ext4_vnop_close },
	{ &vnop_getattr_desc,  (VOPFUNC)ext4_vnop_getattr },
	{ &vnop_setattr_desc,  (VOPFUNC)ext4_vnop_setattr },
	{ &vnop_read_desc,     (VOPFUNC)ext4_vnop_read },
	{ &vnop_write_desc,    (VOPFUNC)ext4_vnop_write },
	{ &vnop_remove_desc,   (VOPFUNC)ext4_vnop_remove },
	{ &vnop_rename_desc,   (VOPFUNC)ext4_vnop_rename },
	{ &vnop_symlink_desc,  (VOPFUNC)ext4_vnop_symlink },
	{ &vnop_link_desc,     (VOPFUNC)ext4_vnop_link },
	{ &vnop_readdir_desc,  (VOPFUNC)ext4_vnop_readdir },
	{ &vnop_readlink_desc, (VOPFUNC)ext4_vnop_readlink },
	{ &vnop_pagein_desc,   (VOPFUNC)ext4_vnop_pagein },
	{ &vnop_pageout_desc,  (VOPFUNC)ext4_vnop_pageout },
	{ &vnop_blockmap_desc, (VOPFUNC)ext4_vnop_blockmap },
	{ &vnop_rmdir_desc,    (VOPFUNC)ext4_vnop_rmdir },
	{ &vnop_inactive_desc, (VOPFUNC)ext4_vnop_inactive },
	{ &vnop_fsync_desc,    (VOPFUNC)ext4_vnop_fsync },
	{ &vnop_reclaim_desc,  (VOPFUNC)ext4_vnop_reclaim },
	{ &vnop_pathconf_desc, (VOPFUNC)ext4_vnop_pathconf },
	{ NULL, NULL }
};

struct vnodeopv_desc ext4_vnodeop_opv_desc = {
	&ext4_vnodeop_p, ext4_vnodeop_entries
};
