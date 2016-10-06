# mHTTP
In today’s Internet, one of the main detriments in user experience is completion times of data transfers that for large objects is limited by network capacity. However, recent developments have opened new opportunities for reducing end-to-end latencies. First, most end-user devices have multiple network interfaces (i. e., interface diversity). Second, popular contents are often available at multiple locations in the network (i. e., data source diversity). When combined, these provide substantial path diversity within the Internet that can be used by users to improve their quality of experience.

Multi-source Multipath HTTP (mHTTP) enables users to establish simultaneous connections with multiple servers to fetch a single content. mHTTP is designed to combine the advantage obtained from distributed network infrastructures provided by CDNs with the advantage of multiple interfaces at end-users. Unlike existing proposals: a) mHTTP is a purely receiver-oriented mechanism that requires no modification either at the server or at the network, b) the modifications are restricted to the socket interface; hence, no changes are needed to the applications or to the kernel, and c) it takes advantage of multiple types of path diversity in the Internet.

The mHTTP design consists of: (i) multiHTTP: a set of modified socket APIs which splits content into multiple chunks, requests each chunk via individual HTTP range requests from the available servers, reassembles the chunks and delivers the content to the application. (ii) multiDNS: a modified DNS resolver that obtains IP addresses for a server name by harvesting the DNS replies and/or by performing multiple lookups of the same server name by contacting different domain name servers.

# Attribution

mHTTP is using http_parser and uthash

    # http_parser
    Based on src/http/ngx_http_parse.c from NGINX copyright Igor Sysoev

    Additional changes are licensed under the same terms as NGINX and
    copyright Joyent, Inc. and other Node contributors. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.



    # uthash
    Copyright (c) 2003-2013, Troy D. Hanson     http://uthash.sourceforge.net
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
    OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
    EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
    PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

