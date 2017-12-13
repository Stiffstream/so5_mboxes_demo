[TOC]

# Что это?
Это несколько демонстрационных примеров, показывающих работу mbox-ов из SObjectizer-5.5.

Внутри репозитория три примера:

* one_to_one_demo. Показывает взаимодействие двух агентов в режиме 1:1 через MPMC-mbox;
* one_to_many_demo. Показывает взаимодействие нескольких агентов в режиме 1:N через MPMC-mbox;
* anti_jitter_mbox_demo. Показывает простую реализацию собственного mbox-а.

# Как взять и попробовать?
**Примечание.** Пример anti_jitter_mbox_demo. использует `std::optional` поэтому требуется C++ компилятор с поддержкой C++17. Пример тестировался на gcc-7.2, clang-5.0 и msvs2017 (vc++15.5). Возможно использование и более старых компиляторов, но в этом случае нужно будет вручную поменять ключи компиляторов (например, использовать -std=c++1z вместо -std=c++17).

Есть два способа попробовать самостоятельно скомпилировать примеры: с помощью CMake и с помощью MxxRu.
## Компиляция с помощью CMake
Для компиляции примеров с помощью CMake требуется загрузить архив с именем вида so5_mboxes_demo-*-full.zip из секции [Downloads](https://bitbucket.org/sobjectizerteam/so5_mboxes_demo/downloads/). После чего:

~~~~~{.sh}
unzip so5_mboxes_demo-201712131230-full.zip
cd so5_mboxes_demo
mkdir cmake_build
cd cmake_build
cmake  -DCMAKE_INSTALL_PREFIX=target -DCMAKE_BUILD_TYPE=Release ../dev
cmake --build . --config Release --target install
~~~~~

Возможно, потребуется дополнительно указать название нужного вам тулчейна через опцию -G. Например:

~~~~~{.sh}
cmake  -DCMAKE_INSTALL_PREFIX=target -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles" ../dev
~~~~~
## Компиляция с помощью MxxRu
Для компиляции с помощью MxxRu потребуется Ruby и RubyGems (как правило, RubyGems устанавливается вместе с Ruby, но если это не так, то RubyGems нужно поставить явно). Для установки MxxRu нужно выполнить команду:

~~~~~{.sh}
gem install Mxx_ru
~~~~~

После того как Ruby, RubyGems и MxxRu установлены можно брать примеры непосредственно из Hg-репозитория:

~~~~~{.sh}
hg clone https://eao197@bitbucket.org/sobjectizerteam/so5_mboxes_demo
cd so5_mboxes_demo
mxxruexternals
cd dev
ruby build.rb
~~~~~

Либо же можно загрузить архив с именем вида so5_mboxes_demo-*-full.zip из секции [Downloads](https://bitbucket.org/sobjectizerteam/so5_mboxes_demo/downloads/). После чего:

~~~~~{.sh}
unzip so5_mboxes_demo-201712131230-full.zip
cd so5_mboxes_demo/dev
ruby build.rb
~~~~~

Скомпилированные примеры должны оказаться внутри подкаталога `target/release`.

Также перед сборкой может быть полезно выполнить:

~~~~~{.sh}
cp local-build.rb.example local-build.rb
~~~~~

и нужным вам образом отредактировать содержимое `local-build.rb`.