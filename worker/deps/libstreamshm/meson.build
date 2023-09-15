fs = import('fs')

#sources = [
#    'src/md5.c',
#    'src/shm_common.c',
#    'src/shm_cyclic_buf.c',
#    'src/shm_mpegts_mux.c',
#    'src/shm_stream.c',
#    'src/shm_stream_registry.c'
#]


#streamshm = static_library(
#    'streamshm',
#    sources: sources,
#    include_directories: include_directories('include'),  
#)


#streamshm_dep = declare_dependency(
#  include_directories: include_directories('include'),
#  link_with: streamshm,
#)


streamshm_lib = 'streamshm'
#streamshm_lib_dir = '../../../../libstreamshm/build/src'
#streamshm_header_dir = '../../../../libstreamshm/build/include'

streamshm_lib_dir = '/usr/local/lib'
streamshm_header_dir = '/usr/local/include'

streamshm_dep = declare_dependency(
    link_args : ['-L' + streamshm_lib_dir, '-latomic', '-static', '-l' + streamshm_lib],
    include_directories : include_directories(streamshm_header_dir))



    
