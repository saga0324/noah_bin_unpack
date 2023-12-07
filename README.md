# noah_bin_unpack

此项目为诺亚舟NP系列Linux学习机的升级包解包程序  
基于 [OpenNoahEdu/np_unpkg](https://github.com/OpenNoahEdu/np_unpkg) 改动

## Build

	make -C src

## Usage

	./src/unpkg <path/to/upgrade.bin> <output_directory>

## New
- 添加解包到指定目录功能
- 添加CRC检查功能
- 优化整体结构和性能
