/*
 * niffs_api.c
 *
 *  Created on: Feb 22, 2015
 *      Author: petera
 */

#include "niffs.h"
#include "niffs_internal.h"

int NIFFS_info(niffs *fs, s32_t *total, s32_t *used, u8_t *overflow) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  if (total == 0 || used == 0 || overflow == 0) return ERR_NIFFS_NULL_PTR;

  *total = (fs->sectors-1) * fs->pages_per_sector * _NIFFS_SPIX_2_PDATA_LEN(fs, 1);
  *used = ((fs->sectors) * fs->pages_per_sector - (fs->free_pages + fs->dele_pages)) * _NIFFS_SPIX_2_PDATA_LEN(fs, 1);
  *overflow = fs->free_pages < fs->pages_per_sector;
  return NIFFS_OK;
}


int NIFFS_creat(niffs *fs, char *name, niffs_mode mode) {
  (void)mode;
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;
  res = niffs_create(fs, name);
  return res;
}

int NIFFS_open(niffs *fs, char *name, u8_t flags, niffs_mode mode) {
  (void)mode;
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res = NIFFS_OK;
  int fd_ix = niffs_open(fs, name, flags);
  if (fd_ix < 0) {
    // file not found
    if (fd_ix == ERR_NIFFS_FILE_NOT_FOUND && (flags & NIFFS_O_CREAT)) {
      res = niffs_create(fs, name);
      if (res == NIFFS_OK) {
        fd_ix = niffs_open(fs, name, flags);
      } else {
        fd_ix = res;
      }
    }
    res = fd_ix;
  } else {
    // file found
    if ((flags & (NIFFS_O_CREAT | NIFFS_O_EXCL)) == (NIFFS_O_CREAT | NIFFS_O_EXCL)) {
      (void)niffs_close(fs, fd_ix);
      return ERR_NIFFS_FILE_EXISTS;
    }
  }

  if (res != NIFFS_OK) return res;

  if (flags & NIFFS_O_TRUNC) {
    niffs_file_desc *fd;
    res = niffs_get_filedesc(fs, fd_ix, &fd);
    if (res != NIFFS_OK) return res;
    niffs_object_hdr *ohdr = (niffs_object_hdr *)_NIFFS_PIX_2_ADDR(fs, fd->obj_pix);
    if (ohdr->len != NIFFS_UNDEF_LEN) {
      // only truncate if file len is > 0
      res = niffs_truncate(fs, fd_ix, 0);
      if (res != NIFFS_OK) {
        (void)niffs_close(fs, fd_ix);
        return res;
      }
      res = niffs_create(fs, name);
      if (res != NIFFS_OK) return res;
      fd_ix = niffs_open(fs, name, flags);
    }
  }

  return res < 0 ? res : fd_ix;
}

int NIFFS_read_ptr(niffs *fs, int fd, u8_t **ptr, u32_t *len) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return niffs_read_ptr(fs, fd, ptr, len);
}

int NIFFS_read(niffs *fs, int fd_ix, u8_t *dst, u32_t len) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;

  int res = NIFFS_OK;
  s32_t read_len = 0;
  do {
    u8_t *rptr;
    u32_t rlen;
    res = niffs_read_ptr(fs, fd_ix, &rptr, &rlen);
    if (res >= 0 && rlen == 0) res = ERR_NIFFS_END_OF_FILE;
    if (res >= 0) {
      u32_t clen = MIN(len, rlen);
      memcpy(dst, rptr, clen);
      dst += clen;
      len -= clen;
      read_len += clen;
      res = niffs_seek(fs, fd_ix, clen, NIFFS_SEEK_CUR);
    }
  } while (len > 0 && res >= 0);

  if (res == ERR_NIFFS_END_OF_FILE && read_len >= 0) {
    return read_len;
  }

  return res == NIFFS_OK ? read_len : res;
}

int NIFFS_lseek(niffs *fs, int fd, s32_t offs, int whence) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return niffs_seek(fs, fd, offs, whence);
}

int NIFFS_remove(niffs *fs, char *name) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;

  int fd = NIFFS_open(fs, name, NIFFS_O_WRONLY, 0);
  if (fd < 0) return fd;
  res = niffs_truncate(fs, fd, 0);
  (void)niffs_close(fs, fd);

  return res;
}

int NIFFS_fremove(niffs *fs, int fd) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return niffs_truncate(fs, fd, 0);
}

int NIFFS_write(niffs *fs, int fd_ix, u8_t *data, u32_t len) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;
  niffs_file_desc *fd;
  res = niffs_get_filedesc(fs, fd_ix, &fd);
  if (res != NIFFS_OK) return res;
  niffs_object_hdr *ohdr = (niffs_object_hdr *)_NIFFS_PIX_2_ADDR(fs, fd->obj_pix);

  s32_t written = 0;
  if (fd->flags & NIFFS_O_APPEND) {
    // always append
    res = niffs_append(fs, fd_ix, data, len);
    written += len;
  } else {
    // check if modify and/or append
    u32_t mod_len = (ohdr->len == NIFFS_UNDEF_LEN ? 0 : ohdr->len) - fd->offs;
    mod_len = MIN(mod_len, len);
    if (mod_len > 0) {
      res = niffs_modify(fs, fd_ix, fd->offs, data, mod_len);
      if (res != NIFFS_OK) return res;
      len -= mod_len;
      data += mod_len;
      written += mod_len;
    }
    if (len > 0) {
      res = niffs_append(fs, fd_ix, data, len);
      written += len;
    }
  }

//  if (res == NIFFS_OK) {
//    res = niffs_seek(fs, fd_ix, len, NIFFS_SEEK_CUR);
//  }

  return res == 0 ? written : res;
}

int NIFFS_fflush(niffs *fs, int fd) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  (void)fd;
  return NIFFS_OK;
}

int NIFFS_stat(niffs *fs, char *name, niffs_stat *s) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;

  int fd = NIFFS_open(fs, name, 0, 0);
  if (fd < 0) return fd;
  res = NIFFS_fstat(fs, fd, s);
  (void)niffs_close(fs, fd);

  return res;
}

int NIFFS_fstat(niffs *fs, int fd_ix, niffs_stat *s) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;

  niffs_file_desc *fd;
  res = niffs_get_filedesc(fs, fd_ix, &fd);
  if (res != NIFFS_OK) return res;

  niffs_object_hdr *ohdr = (niffs_object_hdr *)_NIFFS_PIX_2_ADDR(fs, fd->obj_pix);

  s->obj_id = ohdr->phdr.id.obj_id;
  s->size = ohdr->len == NIFFS_UNDEF_LEN ? 0 : ohdr->len;
  strncpy((char *)s->name, (char *)ohdr->name, NIFFS_NAME_LEN);

  return NIFFS_OK;
}

int NIFFS_ftell(niffs *fs, int fd_ix) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  int res;

  niffs_file_desc *fd;
  res = niffs_get_filedesc(fs, fd_ix, &fd);
  if (res != NIFFS_OK) return res;

  return (int)fd->offs;
}

int NIFFS_close(niffs *fs, int fd) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return niffs_close(fs, fd);
}

int NIFFS_rename(niffs *fs, char *old, char *new) {
  if (!fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return niffs_rename(fs, old, new);
}

niffs_DIR *NIFFS_opendir(niffs *fs, char *name, niffs_DIR *d) {
  (void)name;
  if (!fs->mounted) return 0;
  d->fs = fs;
  d->pix = 0;
  return d;
}

int NIFFS_closedir(niffs_DIR *d) {
  if (!d->fs->mounted) return ERR_NIFFS_NOT_MOUNTED;
  return NIFFS_OK;
}

static int niffs_readdir_v(niffs *fs, niffs_page_ix pix, niffs_page_hdr *phdr, void *v_arg) {
  (void)fs;
  struct niffs_dirent *e = (struct niffs_dirent *)v_arg;
  if (_NIFFS_IS_FLAG_VALID(phdr) && !_NIFFS_IS_FREE(phdr) && !_NIFFS_IS_DELE(phdr)) {
    if (_NIFFS_IS_OBJ_HDR(phdr)) {
      // object header page
      niffs_object_hdr *ohdr = (niffs_object_hdr *)phdr;
      e->obj_id = ohdr->phdr.id.obj_id;
      e->pix = pix;
      e->size = ohdr->len == NIFFS_UNDEF_LEN ? 0 : ohdr->len;
      strncpy((char *)e->name, (char *)ohdr->name, NIFFS_NAME_LEN);
      return NIFFS_OK;
    }
  }
  return NIFFS_VIS_CONT;
}

struct niffs_dirent *NIFFS_readdir(niffs_DIR *d, struct niffs_dirent *e) {
  if (!d->fs->mounted) return 0;
  struct niffs_dirent *ret = 0;

  int res = niffs_traverse(d->fs, d->pix, 0, niffs_readdir_v, e);
  if (res == NIFFS_OK) {
    d->pix = e->pix + 1;
    ret = e;
  } else if (res == NIFFS_VIS_END) {
    // end of stream
  }

  return ret;
}

int NIFFS_chk(niffs *fs) {
  if (fs->mounted) return ERR_NIFFS_MOUNTED;
  return niffs_chk(fs);
}
