LHE README
=============

# LHE
LHE image and video coder and decoder

LHE - Logarithmical Hopping Encoding - is a new spatial domain lossy compressor, based on weber-Feschner law
This project comprises following components:
- LHE basic image compressor/decompressor : non-elegible bit-rate
- Advanced LHE compressor/decompressor: elegible quality

Instalación (es recomendable instalar OpenMP)
=============

gcc -v 
sudo apt-get install gcc-4.2 (solo si el comando anterior nos dio una versión anterior a la 4.2)

sudo apt-get install libgomp1


Compilación
=============

git checkout -b lhe_develop origin/lhe_develop (LHE se encuentra en la rama lhe_develop)

./configure --extra-cflags=-fopenmp --extra-ldflags=-fopenmp (compila con OpenMP)
sudo make && sudo make install 



Ejecución LHE Básico
=============

-basic_lhe true: ejecutará LHE básico. Si no se escribe este parámetro se ejecuta LHE Avanzado
-pix_fmt formato: formato admite los valores yuv420p, yuv422p, yuv444p. Si no se especifica ninguno, FFmpeg elige el que más se adapte a la imagen de entrada.

Para codificar una imagen, con formato YUV420. Por ejemplo Lena.bmp:

ffmpeg -i lena.bmp -basic_lhe true -pix_fmt yuv420p lena.lhe

Para decodificar una imagen, por ejemplo lena.lhe

ffmpeg -i lena.lhe lenadec.bmp

Ejecución LHE Avanzado
=============

-ql valor: valor puede estar en el rango de 0 a 99, indica nivel de calidad.
-pix_fmt formato: formato admite los valores yuv420p, yuv422p, yuv444p. Si no se especifica ninguno, FFmpeg elige el que más se adapte a la imagen de entrada.
-pr_metrics true: imprime las métricas de Relevancia Perceptual (para testing)

Por ejemplo, si se quiere hacer LHE Avanzado con formato YUV 420 y QL 70

ffmpeg -i lena.bmp -ql 70 -pix_fmt yuv420p lena.lhe

Para decodificar la imagen

ffmpeg -i lena.lhe lenadec.bmp

Si se quiere decodificar los planos por separado (YUV)

ffmpeg -i lena.lhe -filter_complex "extractplanes=y+u+v[y][u][v]" -map [y] lenay.bmp -map [u] lenau.bmp -map [v] lenav.bmp


FFmpeg README
=============

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* [ffserver](https://ffmpeg.org/ffserver.html) is a multimedia streaming server
  for live broadcasts.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process. Few developers
follow pull requests so they will likely be ignored.
