const Gio = imports.gi.Gio;

const GenAsm = imports.gi.GenAsm;

let assembler = new GenAsm.Assembler();

log(assembler.gen_version);
log(assembler.assemble('pan'));
