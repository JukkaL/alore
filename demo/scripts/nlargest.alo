-- Usage: nlargest.alo [DIR [N]]
--
-- Find the N largest subdirectories of DIR (default to '.' and 10).

import os


def Main(args)
  var n = 10
  var dir = '.'
  if args != []
    dir = args[0]
    if args.length() > 1
      n = Int(args[1])
    end
  end
  LargestDirs(n, dir)
end


-- Display the n largest subdirectories of dir.
def LargestDirs(n, dir)
  var a = []
  DirSizes(dir, a)
  a = Reversed(Sort(a))
  for size, d in a[:n]
    Print('{-8:} {}'.format(size div 1024, d))
  end
end


-- Append to res a tuple (size, subdir) for each subdirectory of dir and return
-- the total size of files in dir.
def DirSizes(dir, res)
  var size = 0
  for n in ListDir(dir)
    var p = Join(dir, n)
    if IsFile(p)
      size += Stat(p).size
    elif IsDir(p) and not IsLink(p)
      var s = DirSizes(p, res)
      res.append((s, NormPath(p)))
      size += s
    end
  end
  return size
end
