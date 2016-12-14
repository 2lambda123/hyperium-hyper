//! Client Responses

use body::Body;
use header;
use http::{self, Chunk, RawStatus};
use status;
use version;

pub fn new(incoming: http::ResponseHead, body: Option<Body>) -> Response {
    trace!("Response::new");
    let status = status::StatusCode::from_u16(incoming.subject.0);
    debug!("version={:?}, status={:?}", incoming.version, status);
    debug!("headers={:?}", incoming.headers);

    Response {
        status: status,
        version: incoming.version,
        headers: incoming.headers,
        status_raw: incoming.subject,
        body: body,
    }

}

/// A response for a client request to a remote server.
//#[derive(Debug)]
pub struct Response {
    status: status::StatusCode,
    headers: header::Headers,
    version: version::HttpVersion,
    status_raw: RawStatus,
    body: Option<Body>,
}

impl Response {
    /// Get the headers from the server.
    #[inline]
    pub fn headers(&self) -> &header::Headers { &self.headers }

    /// Get the status from the server.
    #[inline]
    pub fn status(&self) -> &status::StatusCode { &self.status }

    /// Get the raw status code and reason.
    #[inline]
    pub fn status_raw(&self) -> &RawStatus { &self.status_raw }

    /// Get the final URL of this response.
    #[inline]
    //pub fn url(&self) -> &Url { &self.url }

    /// Get the HTTP version of this response from the server.
    #[inline]
    pub fn version(&self) -> &version::HttpVersion { &self.version }

    pub fn body(mut self) -> Body {
        self.body.take().unwrap_or(Body::empty())
    }
}

#[cfg(test)]
mod tests {
    /*
    use std::io::{self, Read};

    use url::Url;

    use header::TransferEncoding;
    use header::Encoding;
    use http::HttpMessage;
    use mock::MockStream;
    use status;
    use version;
    use http::h1::Http11Message;

    use super::Response;

    fn read_to_string(mut r: Response) -> io::Result<String> {
        let mut s = String::new();
        try!(r.read_to_string(&mut s));
        Ok(s)
    }


    #[test]
    fn test_into_inner() {
        let message: Box<HttpMessage> = Box::new(
            Http11Message::with_stream(Box::new(MockStream::new())));
        let message = message.downcast::<Http11Message>().ok().unwrap();
        let b = message.into_inner().downcast::<MockStream>().ok().unwrap();
        assert_eq!(b, Box::new(MockStream::new()));
    }

    #[test]
    fn test_parse_chunked_response() {
        let stream = MockStream::with_input(b"\
            HTTP/1.1 200 OK\r\n\
            Transfer-Encoding: chunked\r\n\
            \r\n\
            1\r\n\
            q\r\n\
            2\r\n\
            we\r\n\
            2\r\n\
            rt\r\n\
            0\r\n\
            \r\n"
        );

        let url = Url::parse("http://hyper.rs").unwrap();
        let res = Response::new(url, Box::new(stream)).unwrap();

        // The status line is correct?
        assert_eq!(res.status, status::StatusCode::Ok);
        assert_eq!(res.version, version::HttpVersion::Http11);
        // The header is correct?
        match res.headers.get::<TransferEncoding>() {
            Some(encodings) => {
                assert_eq!(1, encodings.len());
                assert_eq!(Encoding::Chunked, encodings[0]);
            },
            None => panic!("Transfer-Encoding: chunked expected!"),
        };
        // The body is correct?
        assert_eq!(read_to_string(res).unwrap(), "qwert".to_owned());
    }

    /// Tests that when a chunk size is not a valid radix-16 number, an error
    /// is returned.
    #[test]
    fn test_invalid_chunk_size_not_hex_digit() {
        let stream = MockStream::with_input(b"\
            HTTP/1.1 200 OK\r\n\
            Transfer-Encoding: chunked\r\n\
            \r\n\
            X\r\n\
            1\r\n\
            0\r\n\
            \r\n"
        );

        let url = Url::parse("http://hyper.rs").unwrap();
        let res = Response::new(url, Box::new(stream)).unwrap();

        assert!(read_to_string(res).is_err());
    }

    /// Tests that when a chunk size contains an invalid extension, an error is
    /// returned.
    #[test]
    fn test_invalid_chunk_size_extension() {
        let stream = MockStream::with_input(b"\
            HTTP/1.1 200 OK\r\n\
            Transfer-Encoding: chunked\r\n\
            \r\n\
            1 this is an invalid extension\r\n\
            1\r\n\
            0\r\n\
            \r\n"
        );

        let url = Url::parse("http://hyper.rs").unwrap();
        let res = Response::new(url, Box::new(stream)).unwrap();

        assert!(read_to_string(res).is_err());
    }

    /// Tests that when a valid extension that contains a digit is appended to
    /// the chunk size, the chunk is correctly read.
    #[test]
    fn test_chunk_size_with_extension() {
        let stream = MockStream::with_input(b"\
            HTTP/1.1 200 OK\r\n\
            Transfer-Encoding: chunked\r\n\
            \r\n\
            1;this is an extension with a digit 1\r\n\
            1\r\n\
            0\r\n\
            \r\n"
        );

        let url = Url::parse("http://hyper.rs").unwrap();
        let res = Response::new(url, Box::new(stream)).unwrap();

        assert_eq!(read_to_string(res).unwrap(), "1".to_owned());
    }

    #[test]
    fn test_parse_error_closes() {
        let url = Url::parse("http://hyper.rs").unwrap();
        let stream = MockStream::with_input(b"\
            definitely not http
        ");

        assert!(Response::new(url, Box::new(stream)).is_err());
    }
    */
}
