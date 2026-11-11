export interface IStorageSink {
  write(data: Uint8Array): Promise<void>;
  finalize(): Promise<Blob | File>;
  abort(): Promise<void>;
}

export class MemoryStorageSink implements IStorageSink {
  private chunks: Uint8Array[] = [];
  private mimeType: string;

  constructor(mimeType: string = 'application/octet-stream') {
    this.mimeType = mimeType;
  }

  async write(data: Uint8Array): Promise<void> {
    this.chunks.push(data);
  }

  async finalize(): Promise<Blob> {
    return new Blob(this.chunks as BlobPart[], { type: this.mimeType });
  }

  async abort(): Promise<void> {
    this.chunks.length = 0;
  }
}

export class OpfsStorageSink implements IStorageSink {
  private fileHandle: any = null;
  private writable: any = null;
  private syncHandle: any = null;
  private relativePath: string;

  constructor(relativePath: string) {
    this.relativePath = relativePath;
  }

  async init(): Promise<void> {
    if (typeof navigator === 'undefined' || !navigator.storage?.getDirectory) {
      throw new Error('OPFS not supported in this environment');
    }
    const root = await navigator.storage.getDirectory();
    const parts = this.relativePath.split('/');
    let currentDir = root;
    for (let i = 0; i < parts.length - 1; i++) {
      if (parts[i]) {
        currentDir = await currentDir.getDirectoryHandle(parts[i], { create: true });
      }
    }
    const leafName = parts[parts.length - 1] || 'unnamed.bin';
    this.fileHandle = await currentDir.getFileHandle(leafName, { create: true });

    if (typeof this.fileHandle.createWritable === 'function') {
      this.writable = await this.fileHandle.createWritable();
    } else if (typeof this.fileHandle.createSyncAccessHandle === 'function') {
      this.syncHandle = await this.fileHandle.createSyncAccessHandle();
    } else {
      throw new Error('Neither createWritable nor createSyncAccessHandle supported on OPFS file handle');
    }
  }

  async write(data: Uint8Array): Promise<void> {
    if (this.writable) {
      await this.writable.write(data);
    } else if (this.syncHandle) {
      this.syncHandle.write(data);
    } else {
      throw new Error('OpfsStorageSink not initialized or stream closed');
    }
  }

  async finalize(): Promise<File> {
    if (this.writable) {
      await this.writable.close();
      this.writable = null;
    } else if (this.syncHandle) {
      this.syncHandle.flush();
      this.syncHandle.close();
      this.syncHandle = null;
    }
    if (!this.fileHandle) {
      throw new Error('No file handle to finalize');
    }
    return await this.fileHandle.getFile();
  }

  async abort(): Promise<void> {
    if (this.writable) {
      try {
        await this.writable.abort();
      } catch {
        // Ignore abort error
      }
      this.writable = null;
    }
    if (this.syncHandle) {
      try {
        this.syncHandle.close();
      } catch {
        // Ignore close error
      }
      this.syncHandle = null;
    }
  }
}

export async function createStorageSink(
  relativePath: string,
  preferOpfs: boolean = false,
  mimeType?: string
): Promise<IStorageSink> {
  if (preferOpfs && typeof navigator !== 'undefined' && typeof navigator.storage?.getDirectory === 'function') {
    try {
      const opfs = new OpfsStorageSink(relativePath);
      await opfs.init();
      return opfs;
    } catch {
      // Fallback to memory sink
    }
  }
  return new MemoryStorageSink(mimeType);
}
