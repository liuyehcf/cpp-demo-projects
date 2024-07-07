```sh
pushd java
mvn clean package -DskipTests
popd

mkdir -p build

# libjvm.so may be in (${JAVA_HOME}/lib/server, ${JAVA_HOME}/jre/lib/amd64/server)
JVM_SO_PATH=$(find $(readlink -f ${JAVA_HOME}) -name "libjvm.so")
JVM_SO_PATH=${JVM_SO_PATH%/*}

C_INCLUDE_PATH=${JAVA_HOME}/include:${JAVA_HOME}/include/linux:${C_INCLUDE_PATH} \
CPLUS_INCLUDE_PATH=${JAVA_HOME}/include:${JAVA_HOME}/include/linux:${CPLUS_INCLUDE_PATH} \
LIBRARY_PATH=${JVM_SO_PATH}:${LIBRARY_PATH} \
gcc -o build/jni_spring_fat_jar_demo jni_spring_fat_jar_demo.cpp -lstdc++ -std=gnu++17 -ljvm

LD_LIBRARY_PATH=${JVM_SO_PATH} build/jni_spring_fat_jar_demo normal java/target/test-spring-fatjar.jar org.apache.hadoop.fs.LocalFileSystem
LD_LIBRARY_PATH=${JVM_SO_PATH} build/jni_spring_fat_jar_demo spring java/target/test-spring-fatjar.jar org.apache.hadoop.fs.LocalFileSystem,org.liuyehcf.Main
```
