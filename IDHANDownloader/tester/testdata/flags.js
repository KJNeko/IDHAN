export const manifest = {
    id: "test.flags",
    version: 1,
    flags: [
        {
            name: "test.enabled",
            description: "Exercise a configurable parser flag.",
            default: false,
        },
    ],
};

export function test(input) {
    const expected = new URL(input.url).pathname === "/enabled";

    if (input.flags["test.enabled"] !== expected) {
        throw new Error(`Expected test.enabled=${expected}, received ${input.flags["test.enabled"]}`);
    }

    return {complete: true};
}
