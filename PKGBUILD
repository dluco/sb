pkgname=sb
pkgver=0.2
pkgrel=1
pkgdesc='A simple webkit/gtk browser, in the style of surf and midori'
arch=('i686' 'x86_64')
url='http://http://dluco.github.io/sb/'
license=('MIT')
depends=('gtk2' 'libwebkit')
makedepends=('pkgconfig')
source=("${pkgname}-${pkgver}.tar.gz")
md5sums=('2543da2200a9fe8d868e884f3b82bffd')

build() {
	cd "${srcdir}/${pkgname}-${pkgver}"

	make
}

package() {
	cd "${srcdir}/${pkgname}-${pkgver}"

	make PREFIX=/usr DESTDIR="${pkgdir}" install
}
