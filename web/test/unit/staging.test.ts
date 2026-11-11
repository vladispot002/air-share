import { describe, it, expect } from 'vitest';

describe('TC-OUTGOING-STAGING: Outgoing File Staging Logic', () => {
  interface MockFile {
    name: string;
    size: number;
  }

  function createStagingManager() {
    const staged: MockFile[] = [];

    const addFiles = (files: MockFile[]) => {
      for (const f of files) {
        const exists = staged.some((s) => s.name === f.name && s.size === f.size);
        if (!exists) {
          staged.push(f);
        }
      }
    };

    const removeFile = (index: number) => {
      if (index >= 0 && index < staged.length) {
        staged.splice(index, 1);
      }
    };

    const clearAll = () => {
      staged.length = 0;
    };

    const getTotalBytes = () => staged.reduce((acc, f) => acc + f.size, 0);

    return { staged, addFiles, removeFile, clearAll, getTotalBytes };
  }

  it('stages single and multiple files correctly', () => {
    const mgr = createStagingManager();
    mgr.addFiles([
      { name: 'document.pdf', size: 1048576 },
      { name: 'photo.jpg', size: 2097152 },
    ]);

    expect(mgr.staged.length).toBe(2);
    expect(mgr.staged[0].name).toBe('document.pdf');
    expect(mgr.staged[1].name).toBe('photo.jpg');
    expect(mgr.getTotalBytes()).toBe(3145728);
  });

  it('prevents accidental duplicate file staging', () => {
    const mgr = createStagingManager();
    mgr.addFiles([{ name: 'report.docx', size: 50000 }]);
    mgr.addFiles([{ name: 'report.docx', size: 50000 }]);

    expect(mgr.staged.length).toBe(1);
  });

  it('removes a staged file by index', () => {
    const mgr = createStagingManager();
    mgr.addFiles([
      { name: 'file1.txt', size: 100 },
      { name: 'file2.txt', size: 200 },
      { name: 'file3.txt', size: 300 },
    ]);

    mgr.removeFile(1); // Remove file2.txt
    expect(mgr.staged.length).toBe(2);
    expect(mgr.staged[0].name).toBe('file1.txt');
    expect(mgr.staged[1].name).toBe('file3.txt');
    expect(mgr.getTotalBytes()).toBe(400);
  });

  it('clears all staged files cleanly', () => {
    const mgr = createStagingManager();
    mgr.addFiles([
      { name: 'archive.tar.gz', size: 10000000 },
      { name: 'video.mp4', size: 50000000 },
    ]);
    expect(mgr.staged.length).toBe(2);

    mgr.clearAll();
    expect(mgr.staged.length).toBe(0);
    expect(mgr.getTotalBytes()).toBe(0);
  });
});
