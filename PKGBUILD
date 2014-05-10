pkgname=sb
pkgver=0.1
pkgrel=1
pkgdesc='A simple webkit/gtk browser, in the style of surf and midori'
arch=('i686' 'x86_64')
url='http://http://dluco.github.io/sb/'
license=('MIT')
depends=('gtk2' 'libwebkit')
makedepends=('pkgconfig')
source=("${pkgname}-${pkgver}.tar.gz")
md5sums=('fec0a4709cb25a89578ec4f2141d5831')

build() {
	cd "${srcdir}/${pkgname}-${pkgver}"

	make
}

package() {
	cd "${srcdir}/${pkgname}-${pkgver}"

	make PREFIX=/usr DESTDIR="${pkgdir}" install
}
