# Build and run
cmake -DCMAKE_BUILD_TYPE=Release -B build-2
cmake --build build-2 --config Release
cp build-2/trader-2 .
python3 rtg.py run trader-2
