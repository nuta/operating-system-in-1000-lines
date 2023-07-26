import { ImageResponse } from "@vercel/og";

export const runtime = "edge";

export async function GET(request: Request) {
    const fontData = await fetch(
        new URL('../../../assets/Carter_One/CarterOne-Regular.ttf', import.meta.url),
      ).then((res) => res.arrayBuffer());

    return new ImageResponse(
    (
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          justifyContent: "center",
          alignItems: "center",
          textAlign: "center",
          width: "100%",
          height: "100%",
          backgroundColor: "#1a1a1a",
          color: "#fdfdfd",
          fontSize: "70px",
          fontFamily: "CarterOne",
          paddingTop: "100px",
          paddingBottom: "200px",
          paddingLeft: "40px",
          paddingRight: "40px",
        }}
      >
        <p>Writing an Operating System</p>
        <p>in</p>
        <p>1,000 Lines</p>
      </div>
    ),
    {
      width: 1200,
      height: 600,
      fonts: [
        {
          name: 'CarterOne',
          data: fontData,
          style: 'normal',
        },
      ],
    }
  );
}
