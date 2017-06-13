#!/usr/bin/env python
# -*- coding: latin-1; -*-
#
# run.py -n 2 Playgrounds\CrashCube_Geom.kzr

import os, sys, subprocess, platform, datetime


import argparse
parser = argparse.ArgumentParser()  
parser.add_argument("-n", help="nb of mpi procs", type=int, default=1)
parser.add_argument("-j", help="nb of threads/procs", type=int, default=1)
parser.add_argument('files', nargs='*', help='test files')
args = parser.parse_args()     


geofile = args.files[0].replace('Para.kzr','Geom.kzr')
parfile = args.files[0].replace('Geom.kzr','Para.kzr')
testname = args.files[0].replace('Geom.kzr','').replace('Para.kzr','').strip('_')

print "geofile=",geofile
print "parfile=",parfile
print "testname=",testname
#raw_input()

nproc = args.n
nthread = args.j

resdirname = testname.replace(os.sep,'_')+datetime.datetime.now().strftime("-%y%m%d-%H%M%S")
resfilename = 'p'


thisdir = os.path.split(__file__)[0]

# look for builddir & sph.exe    
builddir = os.path.join(thisdir,'build')
if not os.path.isdir(builddir): raise Exception('%s not found!' % builddir)

if 'Windows' in platform.uname():
    exefile = os.path.join(builddir,'Release','sph.exe')
else:
    exefile = os.path.join(builddir,'sph')
if not os.path.isfile(exefile): raise Exception('%s not found!' % exefile)

# look for test files
#parfile = os.path.join(thisdir,'%s_Para.kzr' % testname)
#geofile = os.path.join(thisdir,'%s_Geom.kzr' % testname)
if not os.path.isfile(parfile): raise Exception('%s not found!' % parfile)
if not os.path.isfile(geofile): raise Exception('%s not found!' % geofile)
   
# build myExperiment
resdir = os.path.join('Results', resdirname)
if os.path.isdir(resdir): raise Exception('%s already exists' % resdir)
print 'Creating %s' % resdir
os.mkdir(resdir)

# set num threads
import os
os.environ['OMP_NUM_THREADS'] = '%d' % nthread

# build command line 
if 'Windows' in platform.uname():
    mpiexe = 'mpiexec -n %d' % nproc
else:
    #mpiexe = 'mpirun -np %d -cpus-per-proc %d'  % (nproc, nthread)
    mpiexe = 'mpirun -np %d -map-by node:PE=%d:OVERSUBSCRIBE --bind-to core:overload-allowed'  % (nproc, nthread)
    
cmd = '%s %s %s %s %s' % (mpiexe, exefile, parfile, geofile, os.path.join(resdirname, resfilename))
print cmd
    
subprocess.call(cmd, shell=True)

