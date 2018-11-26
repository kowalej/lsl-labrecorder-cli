#%%

'''
 ___                _         
|                             
|           |-      +    -    
|     | |   |       |   | |   
 ---   --                --   
                               
<------- ____
  &&&    /    \  __ _____,
    `-- |      \'  `  &&/
       `|  o    | o  },-'
         \____( )__/
         ,'    \'   \
 /~~~~~~|.      |   .}~~~\
  ,-----( .     |   .}--.
        | .    /\___/
         `----^,\ \
                \_/
                
'''

#%%

import pandas as pd
import numpy as np
import sys, os

# add script directory to path then import xdf           
pathname = os.path.dirname(sys.argv[0])        
sys.path.append(os.path.abspath(pathname))
from xdf import load_xdf

#%%

write_csvs          = True
lsl_xdf_path        = sys.argv[1]

#lsl_xdf_path        = 'test5.xdf'
#lsl_xdf_path        = 'Test-Roy.xdf'
#lsl_xdf_path        = 'untitled.xdf'
#lsl_xdf_path        = 'mouse_test.xdf'
#lsl_xdf_path        = 'HookSampleMouseKeyTest.xdf'
#lsl_xdf_path        = 'MouseClockwiseFromUpperLeft.xdf'
#lsl_xdf_path        = 'TheAlphabetAndSomeMouse.xdf'
#lsl_xdf_path        = 'Halloween.xdf'
#lsl_xdf_path        = 'MouseClockwiseFromMouseKeyBroadcaster.xdf'
#lsl_xdf_path        = 'MouseBroadcasterRunningOnYOGA.xdf'
#lsl_xdf_path        = 'MouseOnYOGA_int32.xdf'
#lsl_xdf_path        = 'MouseTraverseAnd5Clicks.xdf'
#lsl_xdf_path        = 'MouseEventsAsStringArray.xdf'
#lsl_xdf_path        = 'test20181112.xdf'
#lsl_xdf_path        = 'test.xdf'

#%%

if not lsl_xdf_path:
    print 'need to specify path to xdf file or folder!'
    exit

if not os.path.exists(lsl_xdf_path):
    print '{} not found!'.format(lsl_xdf_path)
    exit

results_folder = lsl_xdf_path.replace('.xdf','')
if not os.path.exists(results_folder):
    os.makedirs(results_folder)
    
#%%

print
print 'loading {}'.format(lsl_xdf_path)
lsl_data            = load_xdf(lsl_xdf_path,verbose=False,synchronize_clocks=False)
streams, fileheader = lsl_data

nstreams = len(streams) 
nstream_string = '{} {} {}'.format('is' if nstreams==1 else 'are',nstreams,'stream' if nstreams==1 else 'streams')
print 'there {} in this recording'.format(nstream_string)

for stream in streams:
    
    print('\n\n'+'-' * 80)
    stream_name=stream['info']['name'][0]
    print('stream',stream_name)
    print('-'*80)
    
    desc = stream['info']['desc']
    print('\ndesc:')
    print desc
    
    stream_data       = stream['time_series']
    stream_data = np.array(stream_data)
    stream_timestamps = stream['time_stamps']
    print('data array shape: {}'.format(stream_data.shape))

    channel_names_list = desc[0]['channels'][0]['channel']
    
    print('\ncolumns:')
    stream_colnames = []
    for cn in channel_names_list:
        name = cn['label'][0]
        stream_colnames.append(name)
        print '  {}'.format(name)
        
    df_stream = pd.DataFrame(data=stream_data,columns=stream_colnames)
    df_stream['lsl_time_stamp'] = stream_timestamps
    
    if write_csvs:
        stream_csv_name = '{}.csv'.format(stream_name.replace(' ','_'))
        stream_csv_path = os.path.join(results_folder,stream_csv_name)
        df_stream.to_csv(stream_csv_path)
