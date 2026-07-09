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
	if (error)
		return (cnp->cn_nameiop == LOOKUP || cnp->cn_nameiop == CREATE) ?
		    ENOENT : error;

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
	{ &vnop_open_desc,     (VOPFUNC)ext4_vnop_open },
	{ &vnop_close_desc,    (VOPFUNC)ext4_vnop_close },
	{ &vnop_getattr_desc,  (VOPFUNC)ext4_vnop_getattr },
	{ &vnop_read_desc,     (VOPFUNC)ext4_vnop_read },
	{ &vnop_readdir_desc,  (VOPFUNC)ext4_vnop_readdir },
	{ &vnop_readlink_desc, (VOPFUNC)ext4_vnop_readlink },
	{ &vnop_pagein_desc,   (VOPFUNC)ext4_vnop_pagein },
	{ &vnop_inactive_desc, (VOPFUNC)ext4_vnop_inactive },
	{ &vnop_reclaim_desc,  (VOPFUNC)ext4_vnop_reclaim },
	{ &vnop_pathconf_desc, (VOPFUNC)ext4_vnop_pathconf },
	{ NULL, NULL }
};

struct vnodeopv_desc ext4_vnodeop_opv_desc = {
	&ext4_vnodeop_p, ext4_vnodeop_entries
};
