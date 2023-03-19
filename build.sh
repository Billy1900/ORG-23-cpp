i=3
rm trader-$i
rm trader-$i.log
# Build and run
cmake -DCMAKE_BUILD_TYPE=Release -B build-$i
cmake --build build-$i --config Release
cp build-$i/trader-$i .
python3.11 rtg.py run trader-$i
