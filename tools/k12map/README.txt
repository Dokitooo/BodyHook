k12map - how k12builds.h was made (1.11.21-ML)

Images (mapped at 0x400000): ../img/A.bin = decrypted 1.0x RLD exe (2K12 dump build),
../img/B.bin / ../img/C.bin = the 1.0 original / SecuROM exes mapped from disk (C: low code encrypted).
  pass1.py  code + call sites: masked signatures of build A found once in B / C
  pass2.py  data addresses: through the instructions of A that use them (majority vote)
  pass3.py  vtables / accessory table by their contents
  build.py  -> resolved.json (+ hand-checked values), check.py = every install check of k12port.inc
  gen.py    resolved.json -> ../../src/k12builds.h
  np.py / hair.py  NBA patch scan + Hair Style item checks per build
